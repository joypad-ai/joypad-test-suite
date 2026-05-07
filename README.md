# Joypad Test Suite

A collection of homebrew controller test ROMs across game consoles. Each console gets its own self-contained subdirectory with its own toolchain and build.

## Consoles

| Console | Status | Path |
|---|---|---|
| GameCube | working | [`gamecube/`](gamecube/) |

## What it tests

- All four/whatever ports simultaneously, no active-port toggling.
- Per-port detected controller type.
- Buttons, analog sticks, triggers, C-stick / C-buttons, D-pad — visualized live.
- Native + cross-protocol controllers (e.g. N64 controllers via passive adapter on GameCube, where supported).
- Accessory detection where applicable (Memory Pak / Rumble Pak / Transfer Pak on N64-protocol controllers).
- Rumble actuation (controllers with built-in motors plus Rumble Pak via accessory protocol).

## Per-console

See each console's `README.md` for build/flash instructions.

## License

[zlib](LICENSE.md). The `gamecube/` subdir is a derivative of corenting's [GC-Controller-Test](https://github.com/corenting/GC-Controller-Test); their copyright is preserved.
