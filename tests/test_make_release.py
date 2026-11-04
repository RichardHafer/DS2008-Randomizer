import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SPEC = importlib.util.spec_from_file_location("make_release", Path(__file__).resolve().parents[1] / "tools/make_release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for folder in ("assets", "assets_pool/pool", "release", "src", "configurator", "build"):
            (self.root / folder).mkdir(parents=True, exist_ok=True)
        for path in ("assets/sup_expl.str", "assets/sup_expl_audio.str", "assets_pool/ordinary.str",
                     "assets_pool/pool/leaper.str"):
            (self.root / path).write_bytes(path.encode())
        self.files = [(self.root / "plugin.asi", "plugins/DeadSpace2008Randomizer.asi")]
        self.files[0][0].write_bytes(b"plugin")
        for name, value in {"ROOT": self.root, "RELEASE": self.root / "release",
                            "ASSET_LOCK": self.root / "release/pool-assets.json", "FILES": self.files}.items():
            patcher = patch.object(release, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        release.freeze_assets("1.0.0")

    def test_changed_assets_require_new_version(self):
        (self.root / "assets_pool/ordinary.str").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "differ from version"):
            release.check_assets(release.asset_rows(), release.load_assets())
        with self.assertRaisesRegex(ValueError, "higher pool version"):
            release.freeze_assets("1.0.0")
        release.freeze_assets("1.0.1")
        release.check_assets(release.asset_rows(), release.load_assets())

    def test_missing_assets_refused(self):
        (self.root / "assets/sup_expl.str").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            release.check_assets(release.asset_rows(), release.load_assets())

    def test_package_boundaries_and_metadata(self):
        lock = release.load_assets()
        plugin = self.root / "Plugin.zip"
        assets = self.root / "PoolAssets.zip"
        release.write_package(plugin, self.files, {release.PLUGIN_MANIFEST: release.json_bytes(release.plugin_metadata("1.0.4", lock))})
        release.write_package(assets, release.asset_rows(), {release.POOL_MANIFEST: release.json_bytes(lock)})
        with zipfile.ZipFile(plugin) as p, zipfile.ZipFile(assets) as a:
            self.assertFalse(set(p.namelist()) & set(a.namelist()))
            self.assertFalse(any(name.endswith(".str") for name in p.namelist()))
            self.assertFalse(any(name.endswith((".asi", ".ini", ".exe", ".dll")) for name in a.namelist()))
            metadata = json.loads(p.read(release.PLUGIN_MANIFEST))
            self.assertEqual(metadata["version"], "1.0.4")
            self.assertEqual(metadata["requires_pool_assets"]["version"], "1.0.0")
            self.assertEqual(metadata["requires_pool_assets"]["manifest_sha256"],
                             release.hashlib.sha256(a.read(release.POOL_MANIFEST)).hexdigest())

    def test_asset_package_stays_identical_for_plugin_updates(self):
        lock = release.load_assets()
        path = self.root / "PoolAssets.zip"
        extra = {release.POOL_MANIFEST: release.json_bytes(lock)}
        release.write_package(path, release.asset_rows(), extra)
        before = path.read_bytes()
        release.plugin_metadata("1.0.5", lock)
        self.files[0][0].write_bytes(b"new plugin")
        release.write_package(path, release.asset_rows(), extra)
        self.assertEqual(before, path.read_bytes())

    def test_plugin_can_be_packaged_without_pool_files(self):
        (self.root / "src/menu_label.inc").write_text('constexpr char kRandomizerVersion[] = "1.0.4";')
        (self.root / "configurator/App.cs").write_text('const string AppVersion = "1.0.4";')
        (self.root / "release/DeadSpace2008Randomizer.ini").write_text('[DeadSpace2008Randomizer]\nGodMode=0\n')
        (self.root / "DeadSpace2008Randomizer.asi").write_bytes(b"plugin")
        (self.root / "build/DeadSpace2008RandomizerConfig.exe").write_bytes(b"config")
        for source, _ in release.asset_rows():
            source.unlink()
        self.assertEqual(release.main(["--package", "plugin", "--version", "1.0.4", "--out-dir", str(self.root)]), 0)
        self.assertTrue((self.root / "DeadSpace2008Randomizer_Plugin_v1.0.4.zip").is_file())

    def test_unsafe_duplicate_and_private_paths_refused(self):
        source = self.files[0][0]
        for name in ("../escape", "C:/escape", "ds1_randomizer_assets/dlc/suit.str", "ds1_randomizer_assets/flierb.str"):
            with self.assertRaises(ValueError):
                release.validate_rows([(source, name)])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            release.validate_rows([(source, "file"), (source, "FILE")])


if __name__ == "__main__":
    unittest.main()
