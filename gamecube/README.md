# Joypad Test Suite — GameCube

The GameCube/Wii build of the [Joypad Test Suite](../README.md). Tests both
native GameCube controllers and N64 controllers (via passive
N64-to-GameCube adapter), including accessory-pak detection (Memory Pak,
Rumble Pak, Transfer Pak) and rumble actuation.

## Status display

Each port shows live state on screen:

```
Port N Style: GCN/N64/None  Pak: ...  Rumble: Idle/Active/Unavailable
Stick: +XXX,+YYY  C-Stick: +XXX,+YYY  L-Trig:NNN  R-Trig:NNN
A:_ B:_ X:_ Y:_ L:_ R:_ Z:_ Start:_
D-U:_ D-D:_ D-L:_ D-R:_  C-U:_ C-D:_ C-L:_ C-R:_
```

Hold A+B together on any port to fire its rumble (controller motor for GCN,
Rumble Pak for N64).

## Build

With devkitPro installed:

```
TARGET_CONSOLE=gamecube make    # produces joypad-gc-gamecube.dol
TARGET_CONSOLE=wii make         # produces joypad-gc-wii.dol
```

Without devkitPro, the project ships a Docker wrapper that uses the
`ghcr.io/extremscorner/libogc2` image (libogc2 is required for reliable
N64-controller SI detection):

```
./build_docker.sh gamecube
./build_docker.sh wii
./build_docker.sh clean
```

## Banner

`opening.bnr` (the file Swiss displays when browsing the folder) is
generated from `branding/banner.png` by `buildtools/make_banner.py`. Edit
`branding/banner.png` (96×32, RGB) and re-run the script:

```
python3 buildtools/make_banner.py opening.bnr
```

## Loading on hardware

Drop the produced `.dol` and `opening.bnr` into a folder on your Swiss SD
(GC Loader, FlippyDrive, SD2SP2, etc.) named whatever you like, with the
`.dol` renamed to `default.dol`:

```
SD root/
  Joypad Test Suite/
    default.dol      # the GameCube .dol from this build
    opening.bnr      # the banner produced by make_banner.py
```

Swiss reads `opening.bnr` and shows the banner image + description in its
file browser; selecting the folder runs `default.dol`.

## Origin

Derived from corenting's
[GC-Controller-Test](https://github.com/corenting/GC-Controller-Test)
under the [zlib license](../LICENSE.md). N64 detection / poll, accessory
detection, rumble pak control, multi-port simultaneous display, and
banner-image build pipeline are added on top.
