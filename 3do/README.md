# Joypad Tester — 3DO

3DO Opera (Panasonic FZ-1 / FZ-10, GoldStar GDO, Sanyo TRY, etc.)
build of the [Joypad Tester](../README.md). Reads the 3DO control
pad daisy-chain and renders the live button state on screen.

## What it tests

The active 3DO control pad's full 32-bit `DoControlPad` bitfield,
shown as raw hex plus a per-button held-or-not grid:

```
Joypad Tester - 3DO
===================

P1 raw: 0x00000000
P2 raw: 0x00000000

P1 buttons:
.       .
.       .
.       .
.       .
.       .
.
```

| Source | Detected as |
|--------|-------------|
| 3DO control pad (D-pad, A/B/C, Start, Stop, L/R shift) | `DoControlPad` slot 1 / slot 2 |
| 3DO mouse / lightgun / arcade panel | not yet wired (uses different SDK calls) |

Buttons rendered: Up, Down, Left, Right, A, B, C, Start, Stop, L (Left
Shift), R (Right Shift). "Stop" is the X button on the original 3DO
pad — its symbol is the eject-style square.

## trapexit/3do-devkit toolchain

3DO homebrew is built with the Norcroft ARM C/C++ compilers (originally
1990s ARM Ltd / Acorn) bundled in
[trapexit/3do-devkit](https://github.com/trapexit/3do-devkit). The
devkit also ships 3DO Portfolio SDK headers + a small modern C++
helper layer (`BasicDisplay`, `abort_err`, etc.) that we link
against unmodified. We pin to a specific devkit commit
(`e0845bc4` at v0.1.0) inside [`buildtools/Dockerfile`](buildtools/Dockerfile);
bumping it = rebuilding the image.

The compilers are 32-bit x86 binaries (Linux only) — and one
post-compile step (`3doiso`) is a Windows binary the devkit runs via
Wine. The Docker image bakes both: `libc6-i386` for the 32-bit
dynamic loader, `wine32` for the iso composer. On Apple Silicon or
any non-x86 host, the container runs under `--platform=linux/amd64`.

## Build

The toolchain is Docker-only:

```
./build_docker.sh                # build (first run also builds image)
./build_docker.sh clean          # nuke build/
./build_docker.sh rebuild-image  # force toolchain image rebuild
```

`build/joypad-tester.iso` is the in-tree output. CI builds on every
push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

## Loading on hardware

### 3DO emulator (Opera / Phoenix / RetroArch's Opera core)

Drop `joypad_tester_v<ver>.iso` onto the emulator. Opera-based
cores require a 3DO BIOS dump in their `system/` directory
(`panafz1.bin` / `panafz10.bin` / `goldstar.bin` / `sanyotry.bin`).

### Real 3DO hardware

Burn the `.iso` to CD-R (700 MB blank, ISO mode — 3DO Opera
filesystem is iso9660-compatible enough that standard burning tools
handle it). Boot on a chipped console; unmodified retail consoles
won't boot homebrew CDs.

ODE / SD-loader options like the
[Plextor / Polymega-replacement chips](https://3dodev.com/) are
the contemporary path; check 3dodev.com for current hardware notes.

## Releases

Tagged as `3do-v<semver>` from the repo root — see
[`3do/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches `joypad_tester_v<semver>_3do.iso` to each GitHub
Release.

## Origin / credits

Built on Antonio SJ Musumeci's
[3do-devkit](https://github.com/trapexit/3do-devkit) (ISC) — see
[`LICENSE.md`](LICENSE.md). The Joypad Tester source
(`src/main.cpp`) is original; `BasicDisplay` + `abort_err` come
verbatim from the devkit and link in as part of the standard
homebrew build.

3DO Portfolio SDK headers / libraries shipped within the devkit are
originally copyrighted by The 3DO Company and widely redistributed
in the 3DO homebrew scene.

Charles Doty (RasterSoft) wrote the original
**3DO Controller Test Multi** (2016) — released "free of any licences,
credit would be appreciated but not required". Distributed in the
homebrew scene by Aer Fixus. Source is available on
[3dodev.com](https://3dodev.com/software/homebrew/console_tools).
Their 8-pad polling loop + Sprite/CEL rendering modules are the
reference shape for our v0.2 multi-controller + screensaver work
(see `.dev/docs/3do_roadmap.md`). v0.1.0 is a from-scratch bring-up
against the modern trapexit devkit's `BasicDisplay` helper.
