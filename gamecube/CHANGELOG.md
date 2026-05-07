# Joypad Test Suite — GameCube — Changelog

## v0.1.0 — 2026-05-07

Initial release.

- Live multi-port display for all four SI ports simultaneously.
- Native GameCube controller support: buttons, D-pad, analog stick + C-stick, analog triggers, rumble.
- N64 controller support via passive adapter (libogc2 SI detection): A/B/Z/Start, D-pad, L/R, C-buttons, analog stick.
- N64 accessory detection: Memory Pak, Rumble Pak, Transfer Pak, Bio Sensor, Snap Station — using libdragon's probe sequence.
- Rumble actuation on hold-A: built-in motor for GCN, Rumble Pak via accessory write for N64.
- N64 mouse style detection.
- GameCube ASCII keyboard support: detects `SI_GC_KEYBOARD`, polls with 3-byte cmd `0x54`, maps scancodes to key labels (A/B/C, F1-F12, modifiers, arrows, etc.) using the joypad-os reference table.
- Boot banner (`opening.bnr`) for Swiss-GC display, generated from `branding/banner.png` at build time.
- Per-console build via Docker (`./build_docker.sh gamecube|wii`) using `ghcr.io/extremscorner/libogc2`.
