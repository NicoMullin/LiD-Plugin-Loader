"""Build dinput8.dll (the plugin loader), and with --test run the tests against it.

GitHub builds every release with this same script, so anyone can rebuild the
DLL and get the exact same file:

    py -3 -m pip install ziglang==0.16.0
    py -3 build.py --test

The DLL lands in out/dinput8.dll and its SHA-256 is printed; compare it with
the one on the release page.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ZIG_VERSION = "0.16.0"
REPO = Path(__file__).resolve().parent
LOADER = REPO / "loader"
TESTS = REPO / "tests"
OUT = REPO / "out"

# The release package (see package() below).
PACKAGE = "LiD-Plugin-Loader"
PACKAGE_NAME = "Plugin Loader"
PUT_PLUGINS_HERE = ("Put plugin DLLs (for example lid-deathmetals.dll) in this folder. The loader "
                    "loads every .dll in it when the game starts.\r\n")


def zig(*args, cwd):
    subprocess.run([sys.executable, "-m", "ziglang", *map(str, args)], cwd=cwd, check=True)


def check_zig():
    try:
        found = subprocess.run([sys.executable, "-m", "ziglang", "version"],
                               capture_output=True, text=True, check=True).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        sys.exit(f"Zig is missing. Install it with: py -3 -m pip install ziglang=={ZIG_VERSION}")
    if found != ZIG_VERSION:
        sys.exit(f"Zig {found} is installed, but the releases are built with {ZIG_VERSION}; "
                 f"a different version gives a different file. "
                 f"Run: py -3 -m pip install ziglang=={ZIG_VERSION}")


def version():
    text = (LOADER / "version.h").read_text()
    return re.search(r'#define LID_LOADER_VERSION\s+"([^"]+)"', text).group(1)


def build_dll():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()
    zig("cc", "-shared", "-target", "x86_64-windows-gnu", "-O2", "-s", "-o", "dinput8.dll",
        LOADER / "lid_loader.c", LOADER / "dinput8.def", LOADER / "version.rc", cwd=OUT)
    (OUT / "lid_loader.lib").unlink(missing_ok=True)    # import library, not needed
    return OUT / "dinput8.dll"


def test_plugin(name, folder):
    zig("cc", "-shared", "-target", "x86_64-windows-gnu", "-O1", f'-DPLUGIN_NAME=L"{name}"',
        "-o", f"test-{name}.dll", TESTS / "plugin.c", cwd=folder)
    (folder / f"test-{name}.lib").unlink(missing_ok=True)


def run_test(name, mode, dll, exe="BrgGame-Steam.exe", plugins=False):
    """Build a stand-in game exe in its own folder next to a copy of the loader, and run it."""
    folder = OUT / "test" / name
    folder.mkdir(parents=True)
    shutil.copy(dll, folder)
    if plugins:
        found = folder / "lid-plugins"
        found.mkdir()
        test_plugin("a", found)
        test_plugin("b", found)
        (found / "c-broken.dll").write_bytes(b"not a DLL")             # must be skipped, not fatal
        (found / "readme.txt").write_text("not a plugin")
        shutil.copy(found / "test-a.dll", found / "test-c.dll.off")    # switched off by its name
        shutil.copy(dll, found / "dinput8.dll")                        # a stray copy of the loader
        (found / "nested").mkdir()
        shutil.copy(found / "test-a.dll", found / "nested" / "nested.dll")
    zig("cc", "-target", "x86_64-windows-gnu", "-O1", "-o", exe, TESTS / "game.c", cwd=folder)
    print(f"\n--- test: {name}", flush=True)
    return subprocess.run([folder / exe, mode], cwd=folder).returncode == 0



# -- the release package -------------------------------------------------------
#
# One zip with everything the LET IT DIE DB Mod Manager needs to install this
# from its "Get mods" window: lid-package.json saying what is in it, the
# loader's dinput8.dll and an empty lid-plugins/ folder. Unpacked by hand it
# goes straight into the game's Binaries\Win64. Same source, same zip: entries are
# sorted and dated 2026-01-01, so it rebuilds byte for byte.

PACKAGE_TIME = (2026, 1, 1, 0, 0, 0)


def _add(archive, name, data):
    info = zipfile.ZipInfo(name, PACKAGE_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, data)


def package(plugins=(), loader=None):
    """Write out/<PACKAGE>.zip and check it. ``plugins`` and ``loader`` are
    built files in out/."""
    manifest = {
        "format": 1,
        "name": PACKAGE_NAME,
        "version": version(),
        "mods": [],
        "plugins": [Path(p).name for p in plugins],
        "replaces": [],
        "loader": Path(loader).name if loader else "",
    }
    entries = {"lid-package.json": (json.dumps(manifest, indent=2) + "\n").encode()}
    for plugin in plugins:
        entries[f"lid-plugins/{Path(plugin).name}"] = Path(plugin).read_bytes()
    if loader:
        entries[Path(loader).name] = Path(loader).read_bytes()
        entries["lid-plugins/PUT-PLUGINS-HERE.txt"] = PUT_PLUGINS_HERE.encode()

    out = OUT / f"{PACKAGE}.zip"
    with zipfile.ZipFile(out, "w") as archive:
        for name in sorted(entries):
            _add(archive, name, entries[name])
    with zipfile.ZipFile(out) as archive:     # read back what was written
        got = {name: archive.read(name) for name in archive.namelist()}
    if got != entries:
        sys.exit(f"{out.name} does not read back as written")
    print(f"{out.name}: {len(entries)} files, SHA-256 {hashlib.sha256(out.read_bytes()).hexdigest()}")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--test", action="store_true", help="also run the tests")
    parser.add_argument("--tag", help="fail unless this release tag (v1.2.3) matches loader/version.h")
    parser.add_argument("--package", action="store_true",
                        help="also write out/LiD-Plugin-Loader.zip, the release package")
    args = parser.parse_args()

    if args.tag and args.tag != "v" + version():
        sys.exit(f"Tag {args.tag} does not match loader/version.h ({version()}). Update one of them.")
    check_zig()
    dll = build_dll()

    if args.test:
        ok = run_test("plugins", "plugins", dll, plugins=True)
        ok = run_test("no-folder", "no-folder", dll) and ok
        ok = run_test("other-exe", "other-exe", dll, exe="SomeTool.exe", plugins=True) and ok
        shutil.rmtree(OUT / "test")
        if not ok:
            sys.exit("\nTESTS FAILED")

    if args.package:
        package(loader=dll)

    digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    print(f"\ndinput8.dll {version()}  SHA-256 {digest}")


if __name__ == "__main__":
    main()
