# Building and running

How to build the runtime for the PC, the Dreamcast, the PSP and the Wii, start the game from the editor or
the command line, package it, and look into a package.

## Pinned versions

- **Godot 4.7.2-stable**, unmodified. The Makefile expects it in `godot/`: on Windows the
  console executable `Godot_v4.7.2-stable_win64_console.exe`, on Linux
  `Godot_v4.7.2-stable_linux.x86_64`; override with `make GODOT=...`.
- PC on Windows: MSYS2 MINGW64 with gcc and make
  (`pacman -S mingw-w64-x86_64-gcc make diffutils`), and raylib 6.0 as its own release for
  MinGW-w64 (`raylib-6.0_win64_mingw-w64.zip`).
- PC on Linux (x86_64): gcc, make, curl and the OpenGL and X11 headers (Debian/Ubuntu:
  `apt install build-essential curl libgl-dev libx11-dev`), and raylib 6.0 as its own
  release for Linux (`raylib-6.0_linux_amd64.tar.gz`).
- `make raylib` downloads the raylib release of the system into `third_party/`.
- Dreamcast: KallistiOS (the PVR backend was built against the master branch of July 2026),
  plus `mkdcdisc` for disc images, and the kos-port `libwav` (music streaming;
  `make install` in `kos-ports/libwav`). The GLdc port (`libGL`) is only needed for
  `GFX=gldc`. Checked on Linux with the KallistiOS master of 15 November 2026 (9926b70a)
  and its default kos-chain profile (GCC 15.2).
- PSP: the PSP SDK of pspdev (https://github.com/pspdev/pspdev) with `PSPDEV` set and
  `$PSPDEV/bin` on the PATH. Checked with its GCC 15.2 under WSL, and PPSSPP 1.20.4.
- Wii and GameCube: devkitPPC and libogc of devkitPro (https://devkitpro.org), with
  `DEVKITPRO` and `DEVKITPPC` set. Checked with the Docker image `devkitpro/devkitppc`
  (GCC 16.2), which needs no installation, and Dolphin.

## PC

PC, from an MSYS2 MINGW64 shell (Windows) or any shell (Linux) in the repository root:

```
make raylib     # once: download raylib into third_party/
make            # build/pc/retro(.exe) and build/tools/rpkdump(.exe)
make packages   # import assets, export the game scenes into build/pkg/<platform>/
make run        # start the game (main menu)
make test       # exporter, loader and game flow tests
make dist       # zip of the PC game in build/dist/
```

raylib is linked statically, so `build/pc/retro.exe` is one file that needs no DLLs beside
it and starts outside the MSYS2 shell too. The raylib package of MSYS2 cannot be used for
that: its static library is built against `glfw3.dll`. raylib's own release has GLFW built
in. On Linux `build/pc/retro` likewise has raylib and GLFW inside and only links the
system's `libGL` and `libX11` (it runs under Wayland through XWayland). `make raylib`
checks the download against a checksum; `make RAYLIB=<folder>` uses a raylib release that
is somewhere else.

## Running from the editor

Godot's own play buttons would run the scene in Godot, which never runs the game. The
addon puts its own into the toolbar above the viewport instead, with a list next to them
that picks the platform (`pc`, `dreamcast`, `psp` or `wii`; the editor remembers it per project):

- **Run the game** exports every scene directly inside `scenes/` for that platform and
  starts the game in the project's main scene.
- **Run the open scene** exports only that scene and starts in it; the packages of the
  other scenes stay as they are.
- **Stop** ends what the last run started. A new run does that by itself.

Between export and start it runs the platform's build command, if there is one. The
commands are in `retro_local.cfg` in the project folder, a file of the machine it is on
(do not share it). A command is a program followed by its arguments; `{project}` stands
for the project folder, `{scene}` for the name of the scene to start with. A key in
`[<platform>.<os>]` (`windows`, `linux`, `macos`) is taken before the same key in
`[<platform>]`, so one file serves a machine that boots both systems:

```
[pc.windows]
build=["C:/msys64/usr/bin/env.exe", "PATH=/mingw64/bin:/usr/bin", "mingw32-make", "-C", "{project}"]

[pc.linux]
build=["make", "-C", "{project}"]

[dreamcast.windows]
build=["wsl", "--cd", "{project}", "--", "bash", "-c", "source /opt/toolchains/dc/kos/environ.sh && make -j8 -C runtime/platform/dreamcast cdi PADDING=0 BOOT={scene}"]
run=["C:/emulators/flycast.exe", "{project}/build/dc/retro.cdi"]

[dreamcast.linux]
build=["bash", "-c", "source /opt/toolchains/dc/kos/environ.sh && make -j8 -C {project}/runtime/platform/dreamcast cdi PADDING=0 BOOT={scene}"]
run=["/home/me/flycast-x86_64-2.7.appimage", "{project}/build/dc/retro.cdi"]

[psp.windows]
build=["wsl", "--cd", "{project}", "--", "bash", "-c", "export PSPDEV=~/pspdev PATH=~/pspdev/bin:/usr/local/bin:/usr/bin:/bin && make -j8 -C runtime/platform/psp game BOOT={scene}"]
run=["C:/emulators/ppsspp/PPSSPPWindows64.exe", "{project}/build/psp/retro/EBOOT.PBP"]

[psp.linux]
build=["bash", "-c", "export PSPDEV=~/pspdev PATH=~/pspdev/bin:/usr/local/bin:/usr/bin:/bin && make -j8 -C {project}/runtime/platform/psp game BOOT={scene}"]
run=["/usr/bin/PPSSPPSDL", "{project}/build/psp/retro/EBOOT.PBP"]

[wii.windows]
build=["docker", "run", "--rm", "-v", "{project}:/src", "-w", "/src", "devkitpro/devkitppc:latest", "make", "-j8", "-C", "runtime/platform/wii", "game", "BOOT={scene}"]
run=["C:/emulators/dolphin/Dolphin.exe", "-C", "Dolphin.Core.WiiSDCard=True", "-C", "Dolphin.Core.WiiSDCardEnableFolderSync=True", "-C", "Dolphin.General.WiiSDCardSyncFolder={project}/build/wii/sd", "-C", "Dolphin.General.WiiSDCardPath={project}/build/wii/sd.raw", "-b", "-e", "{project}/build/wii/sd/apps/retro/boot.dol"]
```

The PSP commands set the PATH to fixed folders on purpose: the command line passes a shell
on its way, and the PATH that WSL takes over from Windows has spaces and brackets in it.

On Linux KallistiOS runs natively, no WSL; the Flycast AppImage is started directly. An
AppImage needs FUSE 2 (`libfuse2t64` on Ubuntu); without it, start it as
`["/usr/bin/env", "APPIMAGE_EXTRACT_AND_RUN=1", "<flycast>.appimage", ...]`.

Without the file the PC is run with `build/pc/retro(.exe)` as it was last built, and the
Dreamcast run says that a `run` command is missing. A failed export or build is shown
with its messages and nothing is started. The Dreamcast run takes a couple of minutes,
most of it writing the disc image.

The same from the command line:

```
godot --headless --path . --script res://addons/retro_runtime/run_cli.gd -- \
    --platform pc [res://scenes/island.tscn]
```

## Packaging the PC game

**Project > Tools > Package PC Build as Zip** in the editor, or `make dist`, exports every
scene directly inside `scenes/` for the PC and writes `build/dist/<project name>-pc.zip`:

```
<project name>/retro.exe          retro on Linux
<project name>/data/<scene>.rpk
<project name>/data/boot.txt       name of the first scene (the project's main scene)
```

Unpack it anywhere and start `retro.exe` (`retro` on Linux; it keeps its executable bit
when unpacked with `unzip`). The zip holds the runtime of the system it was made on. The runtime is not compiled by this step; build
it with `make` first.

## Dreamcast

Dreamcast, inside the KallistiOS environment (after `make packages` on the PC side):

```
source /opt/toolchains/dc/kos/environ.sh
make -C runtime/platform/dreamcast cdi    # build/dc/retro.cdi, packages on the disc
make -C runtime/platform/dreamcast cdi BOOT=island   # the disc starts in that scene (data/boot.txt)
make -C runtime/platform/dreamcast cdi PADDING=0     # a 13 MB image for emulators instead of a whole
                                         # disc of 740 MB; the play buttons should use this
make -C runtime/platform/dreamcast FPS=1  # also print the real frame rate and the time per frame in
                                         # game logic, drawing and present to the serial output
make -C runtime/platform/dreamcast ROMDISK=1 BUILD=../../../build/dc-test   # bare ELF for tests
make -C runtime/platform/dreamcast GFX=gldc BUILD=../../../build/dc-gldc    # the GLdc backend
```

The program reads its packages from the disc (`/cd/data/<scene>.rpk`), so only the scene
that is shown takes up memory; `flycast build/dc/retro.cdi` runs it. `ROMDISK=1` links the
packages into the ELF instead, which costs their whole size in RAM and is only meant for
test builds that an emulator starts without a disc. A+B+X+Y+START leaves the program.

## PSP

PSP, with the tools of the PSP SDK on the PATH (after `make packages` on the PC side):

```
export PSPDEV=~/pspdev PATH=$PATH:~/pspdev/bin
make -C runtime/platform/psp game                # build/psp/retro/: EBOOT.PBP and data/
make -C runtime/platform/psp game BOOT=island    # the game starts in that scene (data/boot.txt)
make -C runtime/platform/psp                     # only EBOOT.PBP
make -C runtime/platform/psp FPS=1 LOG=1         # print the frame rate, and write what is printed
                                                 # into retro.log next to the program
```

`build/psp/retro/` is the game: copy the folder to `ms0:/PSP/GAME/` on the memory stick of a
console with custom firmware, or open its `EBOOT.PBP` in PPSSPP
(`PPSSPPWindows64.exe build/psp/retro/EBOOT.PBP`). The program reads its packages from the
`data` folder next to it, so only the scene that is shown takes up memory. HOME leaves the
program. [psp-backend.md](psp-backend.md) says how the backend works and why the PSP's
screen shows more to the sides.

## Wii and GameCube

With devkitPro's tools (after `make packages` on the PC side), natively or in its Docker
image:

```
make -C runtime/platform/wii game                 # build/wii/sd/: what goes onto the SD card
make -C runtime/platform/wii game BOOT=island     # the game starts in that scene (data/boot.txt)
make -C runtime/platform/wii CONSOLE=gamecube game   # the same for the GameCube, in build/gamecube/
make -C runtime/platform/wii FPS=1 LOG=1          # print the frame rate, to the console's debug
                                                  # output (Dolphin's log, channel OSREPORT)

docker run --rm -v "$PWD:/src" -w /src devkitpro/devkitppc make -C runtime/platform/wii game
```

`build/wii/sd/` is the content of the SD card: `apps/retro/boot.dol`, which the Homebrew
Channel lists, and the packages in `apps/retro/data/`. Both consoles load the packages of
the platform `wii`.

Dolphin has to be given that folder as the SD card, which it then makes a card image from
each time a game starts (in its window: Config > Wii > SD Card, "Automatically sync with
folder"). The run command above does it for one start with `-C`, which sets a setting
without saving it: Dolphin keeps its own settings, the controllers among them, and its own
SD card. In
Dolphin's default key mapping the arrow keys are the stick, X is A, Z is B, and Enter is
both Start and the HOME button of the Wii Remote, which leaves the program: map HOME to
another key. [wii-backend.md](wii-backend.md) says how the backend works.

## Runtime options

Runtime options on the PC: `retro [scene] [--data dir] [--dump] [--frames N]
[--screenshot file.png] [--input script]`. The input script presses actions at given frames
(`"30:ui_accept,60:accelerate:300"`) and is what the game flow tests use; while a script is
set, real keys and pads are ignored. For a console test build, pass
`INPUT_SCRIPT='...' BUILD=../../../build/dc-test` to the Dreamcast Makefile; the PSP
Makefile takes `INPUT_SCRIPT` the same way.

## Looking into a package

```
build/tools/rpkdump build/pkg/pc/main_menu.rpk     # any little-endian platform
build/pc/retro main_menu --dump                    # same text, from the runtime itself
```


## Repository layout in detail

```
addons/retro_runtime/   exporter, per-platform bakers, editor plugin, command line entry,
                        height_terrain.gd (the HeightTerrain node, an editor helper)
components/             placeholder scripts (declarations only)
scenes/                 main_menu, test_track, sprites_2d, platformer_2d, platformer_3d,
                        racing, island, ball_roll, stunt_track, features;
                        scenes/platformer/ are parts of the 3D level, scenes/parts/ scenes
                        that are instanced at runtime, scenes/tests/ test inputs
assets/                 placeholder images and sounds (made by tools/make_test_assets.gd),
                        assets/kenney/ the tiles and models of the platformer and
                        racing demos;
                        assets/sounds/music.ogg is a test tone made with ffmpeg
runtime/core/           portable C: package loader, scene, components, input, triggers, UI
runtime/platform/       pc/ (raylib), dreamcast/ (KallistiOS: dc_system.c, and dc_pvr.c
                        or dc_gldc.c for the graphics), psp/ (PSP SDK: psp_system.c
                        and psp_gu.c), wii/ (libogc, for the Wii and the
                        GameCube: wii_system.c and wii_gx.c)
runtime/game/           game components
runtime/third_party/    code of others that is part of the runtime: picophysics (the body
                        of the stunt car)
tools/                  rpkdump (C), make_test_assets.gd, make_platformer_2d.gd,
                        make_racing.gd (builds scenes/racing.tscn from track pieces),
                        make_island.gd (builds scenes/island.tscn, the terrain demo),
                        make_ball_roll.gd (builds scenes/ball_roll.tscn, the ball demo),
                        make_stunt_track.gd (builds scenes/stunt_track.tscn, the stunt demo),
                        make_feature_test.gd (builds scenes/features.tscn, a scene
                        that checks runtime features by itself and prints "features: ok")
                        make_stress_test.gd (builds scenes/tests/stress_triggers.tscn: 100
                        areas, 100 bodies, 30 rays, 2 moving bodies, for checking and
                        timing the trigger, ray and body tests)
tests/expected/         expected output of the tree dump test
docs/                   this document and the others (see the README for the list)
```
