# LiD Plugin Loader

A small `dinput8.dll` for Let It Die (Steam) that loads code mods ("plugins")
from one folder, so any number of them can be installed side by side.

```
LET IT DIE\Binaries\Win64\
    BrgGame-Steam.exe
    dinput8.dll                  <- this loader
    lid-plugins\
        lid-classpassives.dll    <- a plugin
        lid-deathmetals.dll      <- another plugin
```

Install the loader once. After that, adding a mod means dropping its plugin
into `lid-plugins`, and removing it means deleting that file. Use one plugin,
several, or none.

## What it does

The game loads `dinput8.dll` from its own folder before the one in Windows.
This one passes every DirectInput call straight through to the real
`dinput8.dll` in the Windows system folder, and when the game starts it loads
each plugin in `lid-plugins`. The loader itself changes nothing in the game:
each plugin makes its own changes and keeps its own log.

Rules, all in [loader/lid_loader.c](loader/lid_loader.c):

- Plugins load only inside `BrgGame-Steam.exe`. Loaded by any other program,
  the loader only passes DirectInput through.
- Only files ending in `.dll` directly in `lid-plugins` are loaded, in name
  order. To switch a plugin off without deleting it, rename it, for example to
  `lid-timers.dll.off`.
- A plugin that fails to load is named in the log and skipped; the others
  still load.
- No network, no registry. The only file it writes is `lid_loader.log` next
  to itself, listing what it loaded.
- Delete `dinput8.dll` and the `lid-plugins` folder and the game is exactly
  stock again.

## Why the DLL is built by GitHub

A DLL can do anything, so you should not have to take one on trust. Every
release on this page is built by GitHub Actions from the source in this
repository, on GitHub's own machines, and the tests run before anything is
published. You can check the DLL in two ways:

1. **Signed build record.** Each release DLL has an attestation: a signed
   record, kept by GitHub, of the exact workflow run and commit that built it.
   With the [GitHub CLI](https://cli.github.com/):

   ```
   gh attestation verify dinput8.dll --repo NicoMullin/LiD-Plugin-Loader
   ```

2. **Rebuild it yourself.** The build is reproducible: the same source and the
   same compiler give a byte-identical file. See
   [Building it yourself](#building-it-yourself) and compare the SHA-256 with
   the one on the release page.

## Installing

**The easy way.** In LiD DB Editor (the LET IT DIE DB Mod Manager) 0.12.6 or
newer, the **Get mods...** window installs the loader for you along with the
first plugin you get, after showing you what it will write. It never replaces
a `dinput8.dll` that is not this loader.

**By hand.**

1. Download `LiD-Plugin-Loader.zip` from the
   [latest release](https://github.com/NicoMullin/LiD-Plugin-Loader/releases/latest).
2. Unzip it into the game's `Binaries\Win64` folder, next to
   `BrgGame-Steam.exe`. That puts `dinput8.dll` there and makes an empty
   `lid-plugins` folder (`lid-package.json` is for the mod manager and can be
   left out).
   - If a `dinput8.dll` is already there, check what it is first. The old LiD
     Fighter Mod's own `dinput8.dll` (version 1.0.0 and older) is replaced by
     this loader plus the Fighter Class Passives plugin, `lid-classpassives.dll`;
     any other one belongs to another mod.
3. Put the plugins you want into `lid-plugins`.

After starting the game, `lid_loader.log` in `Binaries\Win64` lists each
plugin that loaded.

## Writing a plugin

A plugin is an ordinary 64-bit Windows DLL. The loader calls `LoadLibrary` on
it during game start-up, before the game's own code runs, so its `DllMain`
runs with the game's exe already in memory. That is the place to make its
changes. A plugin should follow the same safety rules as the loader and the
existing plugins:

- Find each piece of game code by its exact bytes, and only change it when it
  is found exactly once. After a game update that moves the code, change
  nothing and say so in the log.
- Keep its own log next to itself, so a problem points at one file.
- Don't touch code another plugin changes. If two plugins ever aim at the same
  bytes, the second finds them already changed and must leave them alone.
- Use only what Windows already has loaded (`kernel32`, `user32`): `DllMain`
  runs while Windows is still loading DLLs.

## Building it yourself

You need Windows and Python 3. The compiler is Zig, installed with pip; no
Visual Studio needed.

```
py -3 -m pip install ziglang==0.16.0
py -3 build.py --test
```

The DLL lands in `out\dinput8.dll` and the SHA-256 is printed at the end. Use
Zig 0.16.0 exactly: another version builds a working DLL, but not a
byte-identical one.

`--test` builds stand-ins for the game exe and two small test plugins, and
checks that:

- plugins load in name order, and each is named in the log;
- a broken plugin, files not ending in `.dll`, files in subfolders and a stray
  copy of the loader are skipped without stopping the rest;
- without a `lid-plugins` folder nothing loads and the log says so;
- in a program that is not the game nothing loads and no log is written;
- DirectInput passes through in every case.

## Making a release

1. Set the new version in [loader/version.h](loader/version.h).
2. Push a tag with the same version, for example `v1.0.1`.

GitHub builds, tests and publishes the release. The build stops if the tag and
`version.h` don't match.
