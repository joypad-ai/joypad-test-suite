# Joypad Tester — 3DO — Changelog

## v0.1.0 — 2026-05-12

First release. Bring-up of the 3DO controller test ROM via the
trapexit/3do-devkit toolchain.

### Highlights

- Live P1 button readout: raw 32-bit `DoControlPad` bitfield rendered
  on screen, plus per-button labels for the 11 standard 3DO inputs
  (D-pad, A/B/C, Start, Stop, L/R shifts).
- Two-pad slot reserved in the polling code (`InitControlPad (2)`) —
  P2 hex is shown alongside P1; full P2 button-grid display comes
  next.

Full feature breakdown + build / loading instructions in
[`3do/README.md`](https://github.com/joypad-ai/joypad-tester/blob/main/3do/README.md).
