"""Build the public release zip of the Dead Space 2008 Randomizer (Dead Space 1, 2008) from the repo.

Only files from the repo go in, never from a game folder, so no log, backup, run state or personal recording can
slip in. The console DLC extension (DeadSpace2008Randomizer_ConsoleDLC.asi, ds1_randomizer_assets\\dlc) is a separate release and is
refused here.

    python -I tools/make_release.py --version 1.0.0 [--out <zip>] [--list]

Checks before writing: the plugin is newer than every source file, the configurator is newer than its sources,
the release INI parses and names every key with a value, and no file of the console DLC is in the plan.
"""
import argparse
import configparser
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT
RELEASE = PATCH / "release"

# (source in the repo, path in the zip, why)
FILES = [
    (PATCH / "DeadSpace2008Randomizer.asi", "plugins/DeadSpace2008Randomizer.asi", "the randomizer plugin"),
    (RELEASE / "DeadSpace2008Randomizer.ini", "DeadSpace2008Randomizer.ini", "settings, read from the game folder"),
    (PATCH / "build" / "DeadSpace2008RandomizerConfig.exe", "DeadSpace2008RandomizerConfig.exe", "settings window"),
    (PATCH / "DeadSpace1Progression.tsv", "DeadSpace1Progression.tsv", "schematic rewards manifest"),
    (PATCH / "DeadSpace1Progression.store.tsv", "DeadSpace1Progression.store.tsv", "store list manifest"),
    (RELEASE / "README.txt", "DeadSpace2008Randomizer_README.txt", "install notes"),
    (RELEASE / "winmm.dll", "winmm.dll", "Ultimate ASI Loader 9.7.2: loads plugins/*.asi"),
    (RELEASE / "UltimateASILoader_LICENSE.txt", "UltimateASILoader_LICENSE.txt", "its MIT licence"),
]
ASSET_GLOBS = [(PATCH / "assets_pool", "*.str", "ds1_randomizer_assets/", "enemy pool sidecar archives"),
               (PATCH / "assets_pool" / "pool", "*.str", "ds1_randomizer_assets/pool/", "the pool's own archive copies")]
# Archives shipped outside the pool build: the second Exploder look and its voice bank.
ASSET_FILES = [
    (PATCH / "assets" / "sup_expl.str", "ds1_randomizer_assets/sup_expl.str", "Enhanced Exploder archive"),
    (PATCH / "assets" / "sup_expl_audio.str", "ds1_randomizer_assets/sup_expl_audio.str", "Enhanced Exploder voices"),
]
FORBIDDEN = ("ConsoleDLC", "/dlc/", "assets_dlc", "uniCustom", "uniform_custom")


def sha1(path):
    return hashlib.sha1(path.read_bytes()).hexdigest()


def newest(paths):
    return max((p.stat().st_mtime, p) for p in paths)


def plan():
    rows = [(src, dst, why) for src, dst, why in FILES + ASSET_FILES]
    for folder, mask, prefix, why in ASSET_GLOBS:
        for src in sorted(folder.glob(mask)):
            rows.append((src, prefix + src.name, why))
    return rows


def checks(rows):
    problems = []
    for src, dst, _ in rows:
        if not src.is_file():
            problems.append("missing: %s" % src)
        if any(word.lower() in (dst + str(src)).lower() for word in FORBIDDEN):
            problems.append("console DLC file in a stock release: %s" % dst)
    # Every archive the plugin names must ship, or that feature silently does nothing.
    shipped = {dst.lower() for _, dst, _ in rows}
    sources = [p for p in (PATCH / "src").rglob("*") if p.is_file() and p.suffix in (".cpp", ".h", ".inc")
               and "dlc" not in p.parts and p.name not in ("console_dlc.inc", "uniform_slots.inc")]
    for source in sources:
        for name in re.findall(r'ds1_randomizer_assets\\\\([A-Za-z0-9_]+)"', source.read_text(errors="replace")):
            if "ds1_randomizer_assets/%s.str" % name.lower() not in shipped:
                problems.append("%s names ds1_randomizer_assets/%s, which is not in the package" % (source.name, name))
    plugin = PATCH / "DeadSpace2008Randomizer.asi"
    sources = [p for p in (PATCH / "src").rglob("*") if p.is_file() and p.suffix in (".cpp", ".h", ".inc")
               and "dlc" not in p.parts]
    if plugin.is_file():
        time, newest_source = newest(sources)
        if plugin.stat().st_mtime < time:
            problems.append("the plugin is older than %s: rebuild it (build.bat)" % newest_source.name)
    config = PATCH / "build" / "DeadSpace2008RandomizerConfig.exe"
    if config.is_file():
        time, newest_source = newest(list((PATCH / "configurator").glob("*.cs")))
        if config.stat().st_mtime < time:
            problems.append("the configurator is older than %s: rebuild it (build_configurator.cmd)"
                            % newest_source.name)
    ini = RELEASE / "DeadSpace2008Randomizer.ini"
    if ini.is_file():
        parser = configparser.ConfigParser(strict=True, interpolation=None)
        parser.optionxform = str
        try:
            parser.read_string(ini.read_text(encoding="utf-8-sig"))
        except configparser.Error as error:
            problems.append("release INI does not parse: %s" % error)
        else:
            if "DeadSpace2008Randomizer" not in parser:
                problems.append("release INI has no [DeadSpace2008Randomizer]")
            empty = [k for k, v in parser["DeadSpace2008Randomizer"].items() if not v.strip()] if "DeadSpace2008Randomizer" in parser \
                else []
            if empty:
                problems.append("release INI keys without a value: %s" % ", ".join(empty))
    return problems


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--out")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    out = Path(args.out) if args.out else ROOT / "dist" / ("DeadSpace2008Randomizer_v%s.zip" % args.version)
    rows = plan()
    for src, dst, why in rows:
        print("%-48s %10s  %s" % (dst, src.stat().st_size if src.is_file() else "MISSING", why))
    problems = checks(rows)
    for problem in problems:
        print("ERROR: " + problem)
    if problems:
        return 1
    if args.list:
        print("--list: nothing written")
        return 0
    manifest = ["Dead Space 2008 Randomizer %s - files and SHA-1" % args.version, ""]
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, dst, _ in rows:
            z.write(src, dst)
            manifest.append("%s  %s" % (sha1(src), dst))
        z.writestr("DeadSpace2008Randomizer_files.txt", "\r\n".join(manifest) + "\r\n")
    with zipfile.ZipFile(out) as z:
        bad = z.testzip()
    print("wrote %s (%d files, %.1f MB)%s" % (out, len(rows) + 1, out.stat().st_size / 2 ** 20,
                                               "" if bad is None else ", CORRUPT ENTRY " + bad))
    return 0 if bad is None else 1


if __name__ == "__main__":
    sys.exit(main())
