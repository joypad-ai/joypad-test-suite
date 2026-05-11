# Joypad Tester — PC Engine — Changelog

## v0.1.0 — 2026-05-11

Initial release. Faithful port of dshadoff's `PCE_Mouse_Test` baseline
as a joypad-tester subdir.

- Detects 2-button / 6-button PC Engine pads via `joy(0..4)` and
  reports raw bit patterns for all five potential controller slots
  (single pad on port 0, or up to five pads through a multitap).
- Detects the PC Engine mouse, reports `mouse_x() / mouse_y()` per-frame
  deltas, and accumulates them into absolute X / Y coordinates.
- **I** button (or right mouse button) toggles between mouse-enabled
  mode (decoded mouse state) and raw controller mode (raw joybus
  reads).
- Builds to `joypad-tester.pce` via a Docker image we maintain
  (`pce/buildtools/Dockerfile`, HuC pinned to `uli/huc@52a556a`).
- Source file: `pce/src/joypad_tester.c`. Includes upstream MIT
  copyright header.

### Known limitations

- No on-screen button labels yet — bits are shown as raw hex. The
  GameCube tester's per-port live readout style will land in a follow-
  up.
- No accessory detection / rumble (PCE has no equivalent on the joybus).
