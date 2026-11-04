"""Build independently versioned plugin and pool-asset packages.

python -I tools/make_release.py --version 1.0.4
python -I tools/make_release.py --package plugin --version 1.0.4
python -I tools/make_release.py --package assets
python -I tools/make_release.py --freeze-assets 1.0.0
"""
import argparse
import configparser
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / "release"
ASSET_LOCK = RELEASE / "pool-assets.json"
POOL_MANIFEST = "ds1_randomizer_assets/RandomizerPoolAssets.json"
PLUGIN_MANIFEST = "DeadSpace2008Randomizer_package.json"
FILES = [
    (ROOT / "DeadSpace2008Randomizer.asi", "plugins/DeadSpace2008Randomizer.asi"),
    (RELEASE / "DeadSpace2008Randomizer.ini", "DeadSpace2008Randomizer.ini"),
    (ROOT / "build/DeadSpace2008RandomizerConfig.exe", "DeadSpace2008RandomizerConfig.exe"),
    (ROOT / "DeadSpace1Progression.tsv", "DeadSpace1Progression.tsv"),
    (ROOT / "DeadSpace1Progression.store.tsv", "DeadSpace1Progression.store.tsv"),
    (RELEASE / "README.txt", "DeadSpace2008Randomizer_README.txt"),
    (RELEASE / "winmm.dll", "winmm.dll"),
    (RELEASE / "UltimateASILoader_LICENSE.txt", "UltimateASILoader_LICENSE.txt"),
]
FORBIDDEN = ("consoledlc", "/dlc/", "assets_dlc", "unicustom", "uniform_custom", "flier")


def version_tuple(version):
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("version must have the form major.minor.patch")
    return tuple(map(int, version.split(".")))


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def asset_rows():
    rows = [(ROOT / "assets" / name, "ds1_randomizer_assets/" + name)
            for name in ("sup_expl.str", "sup_expl_audio.str")]
    for folder, prefix in ((ROOT / "assets_pool", "ds1_randomizer_assets/"),
                           (ROOT / "assets_pool/pool", "ds1_randomizer_assets/pool/")):
        paths = sorted(folder.glob("*.str"))
        if not paths:
            raise ValueError("no pool archives in " + str(folder))
        rows.extend((path, prefix + path.name) for path in paths)
    return sorted(rows, key=lambda row: row[1])


def validate_rows(rows):
    seen = set()
    for source, name in rows:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
            raise ValueError("unsafe package path: " + name)
        if name.lower() in seen:
            raise ValueError("duplicate package path: " + name)
        seen.add(name.lower())
        if any(token in name.lower() for token in FORBIDDEN):
            raise ValueError("excluded content in package: " + name)
        if not source.is_file():
            raise ValueError("missing: " + str(source))


def inventory(rows):
    validate_rows(rows)
    return {name: {"bytes": path.stat().st_size, "sha256": sha256(path)} for path, name in rows}


def load_assets():
    lock = json.loads(ASSET_LOCK.read_text(encoding="utf-8"))
    version_tuple(lock["version"])
    if lock.get("schema") != 1 or lock.get("package") != "DeadSpace2008Randomizer_PoolAssets" or not lock.get("files"):
        raise ValueError("invalid pool asset manifest")
    for name, item in lock["files"].items():
        parts = PurePosixPath(name).parts
        if (not name.startswith("ds1_randomizer_assets/") or not name.endswith(".str") or ".." in parts
                or "\\" in name or ":" in name or any(token in name.lower() for token in FORBIDDEN)
                or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) or item["bytes"] <= 0):
            raise ValueError("invalid pool asset entry: " + name)
    return lock


def freeze_assets(version):
    requested = version_tuple(version)
    files = inventory(asset_rows())
    if ASSET_LOCK.exists():
        old = load_assets()
        if version == old["version"] and files == old["files"]:
            print("pool assets " + version + " unchanged")
            return
        if requested <= version_tuple(old["version"]):
            raise ValueError("changed assets require a new, higher pool version")
    lock = {"schema": 1, "package": "DeadSpace2008Randomizer_PoolAssets", "version": version, "files": files}
    ASSET_LOCK.write_bytes(json_bytes(lock))
    print("recorded pool assets %s: %d archives" % (version, len(files)))


def check_assets(rows, lock):
    if inventory(rows) != lock["files"]:
        raise ValueError("pool archives differ from version %s; restore them or use --freeze-assets with a new version"
                         % lock["version"])


def check_plugin(version, lock):
    version_tuple(version)
    validate_rows(FILES)
    sources = [p for p in (ROOT / "src").rglob("*") if p.suffix in (".cpp", ".h", ".inc")]
    if not sources:
        raise ValueError("plugin sources are missing")
    for source in sources:
        for name in re.findall(r'ds1_randomizer_assets\\\\([A-Za-z0-9_]+)"', source.read_text(errors="replace")):
            if "ds1_randomizer_assets/%s.str" % name not in lock["files"]:
                raise ValueError("%s references an archive absent from the pool manifest: %s" % (source.name, name))
    label = (ROOT / "src/menu_label.inc").read_text()
    app = (ROOT / "configurator/App.cs").read_text(encoding="utf-8-sig")
    if 'kRandomizerVersion[] = "%s"' % version not in label or 'AppVersion = "%s"' % version not in app:
        raise ValueError("requested plugin version differs from the plugin/configurator source version")
    for binary, inputs in ((ROOT / "DeadSpace2008Randomizer.asi", sources),
                           (ROOT / "build/DeadSpace2008RandomizerConfig.exe", list((ROOT / "configurator").rglob("*.cs")))):
        newest = max(inputs, key=lambda p: p.stat().st_mtime)
        if binary.stat().st_mtime < newest.stat().st_mtime:
            raise ValueError("%s is older than %s; rebuild it" % (binary.name, newest))
    parser = configparser.ConfigParser(strict=True, interpolation=None)
    parser.read_string((RELEASE / "DeadSpace2008Randomizer.ini").read_text(encoding="utf-8-sig"))
    if "DeadSpace2008Randomizer" not in parser or any(not value.strip() for value in parser["DeadSpace2008Randomizer"].values()):
        raise ValueError("release INI section is missing or contains an empty value")


def plugin_metadata(version, lock):
    return {"schema": 1, "package": "DeadSpace2008Randomizer_Plugin", "version": version,
            "requires_pool_assets": {"version": lock["version"], "manifest": POOL_MANIFEST,
                                     "manifest_sha256": hashlib.sha256(json_bytes(lock)).hexdigest()}}


def write_package(out, rows, extra):
    validate_rows(rows)
    names = {name.lower() for _, name in rows}
    if names.intersection(name.lower() for name in extra):
        raise ValueError("generated metadata collides with a package file")
    manifest = {name: sha256(path) for path, name in rows}
    manifest.update({name: hashlib.sha256(data).hexdigest() for name, data in extra.items()})
    extra = dict(extra)
    extra["%s_SHA256SUMS.txt" % out.stem] = "".join("%s  %s\n" % (digest, name)
        for name, digest in sorted(manifest.items())).encode()
    out.parent.mkdir(parents=True, exist_ok=True)
    temporary = out.with_suffix(".zip.tmp")
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            payloads = {name: path for path, name in rows}
            payloads.update(extra)
            for name, payload in sorted(payloads.items()):
                info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                if isinstance(payload, Path):
                    with payload.open("rb") as source, archive.open(info, "w") as target:
                        shutil.copyfileobj(source, target, 1024 * 1024)
                else:
                    archive.writestr(info, payload)
        with zipfile.ZipFile(temporary) as archive:
            bad = archive.testzip()
            if bad:
                raise ValueError("corrupt ZIP entry: " + bad)
        temporary.replace(out)
    finally:
        if temporary.exists():
            temporary.unlink()
    print("wrote %s (%.2f MiB), SHA-256 %s" % (out, out.stat().st_size / 2**20, sha256(out)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="plugin version; required for plugin/all")
    parser.add_argument("--package", choices=("plugin", "assets", "all"), default="all")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--out", type=Path, help="one ZIP path; requires --package plugin or assets")
    parser.add_argument("--freeze-assets", metavar="VERSION", help="record a new asset version and exit")
    parser.add_argument("--list", action="store_true", help="check and show contents without writing ZIPs")
    args = parser.parse_args(argv)
    if args.freeze_assets:
        freeze_assets(args.freeze_assets)
        return 0
    if args.out and args.package == "all":
        parser.error("use --out-dir for both packages, or select --package plugin/assets with --out")
    if args.package != "assets" and not args.version:
        parser.error("--version is required for the plugin package")
    lock = load_assets()
    jobs = []
    if args.package in ("plugin", "all"):
        check_plugin(args.version, lock)
        out = args.out or args.out_dir / ("DeadSpace2008Randomizer_Plugin_v%s.zip" % args.version)
        jobs.append((out, FILES, {PLUGIN_MANIFEST: json_bytes(plugin_metadata(args.version, lock))}))
    if args.package in ("assets", "all"):
        rows = asset_rows()
        check_assets(rows, lock)
        rows.append((RELEASE / "POOL_ASSETS_README.txt", "DeadSpace2008Randomizer_PoolAssets_README.txt"))
        validate_rows(rows)
        out = args.out or args.out_dir / ("DeadSpace2008Randomizer_PoolAssets_v%s.zip" % lock["version"])
        jobs.append((out, rows, {POOL_MANIFEST: json_bytes(lock)}))
    for out, rows, extra in jobs:
        print("%s: %d files plus metadata" % (out.name, len(rows)))
        if args.list:
            for _, name in rows:
                print("  " + name)
            for name in extra:
                print("  " + name)
        else:
            write_package(out, rows, extra)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, configparser.Error) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        sys.exit(1)
