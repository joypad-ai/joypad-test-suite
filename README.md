# Joypad Tester

A collection of homebrew controller test ROMs across game consoles. Each console gets its own self-contained subdirectory with its own toolchain and build.

## Consoles

| Console | Status | Path | License |
|---|---|---|---|
| GameCube | working | [`gcn/`](gcn/) | [zlib](gcn/LICENSE.md) |
| Game Boy Advance | working | [`gba/`](gba/) | [MIT](gba/LICENSE.md) |
| PC Engine | scaffolded | [`pce/`](pce/) | [MIT](pce/LICENSE.md) |

Subdirs use short 3-letter codenames (`gcn`, `gba`, `pce`, future
`n64`/`snes`/…) matching homebrew-community conventions. Release tags
follow the same prefix: `<codename>-v<semver>`.

## Per-console

Each console subdir has its own `README.md` with what its app tests,
how to build/flash, and any embedding notes. See:

- [`gcn/README.md`](gcn/README.md)
- [`gba/README.md`](gba/README.md)
- [`pce/README.md`](pce/README.md)

## Acknowledgements

Each console app has its own lineage. As more consoles join, this list grows.

| Console | Origin / inspiration |
|---|---|
| GameCube | Derived from [corenting/GC-Controller-Test](https://github.com/corenting/GC-Controller-Test) (zlib). Multi-port layout and accessory probe flow modeled after [meeq/JoypadTest-N64](https://github.com/meeq/JoypadTest-N64). GC keyboard wire format and scancode table come from the [joypad-os](https://github.com/joypad-ai/joypad-os) firmware (`src/lib/joybus-pio`). |
| Game Boy Advance | Joybus handshake + main loop from [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO) (MIT). Eyes overlay ported from [joypad-os](https://github.com/joypad-ai/joypad-os)'s `eyes_anim`. Mode-4 page-flipped screensaver matches the GameCube tester's logo + color cycle. Two ROM variants from one source tree: `joypad_mb.gba` (eyes, for joypad-os submodule consumers) and `tester_mb.gba` (Doridian + on-GBA console). |
| PC Engine | Baseline from [dshadoff/PCE_Mouse_Test](https://github.com/dshadoff/PCE_Mouse_Test) (MIT). Detects 2-button / 6-button pads and the PC Engine mouse, including multitap support. Built with [uli/huc](https://github.com/uli/huc) (HuC) pinned in a Docker image we maintain. |

Originating copyrights are preserved in each console's source headers.

## License

Top-level repo scaffolding (CI, build infra, this README): [MIT](LICENSE.md).
Each console subdir carries its own `LICENSE.md` matching its upstream
origin — see the **License** column of the Consoles table above.
