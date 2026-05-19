# Joypad Tester — 3DO — Changelog

## v1.0.1 — 2026-05-19

Re-release of the 3DO tester. The v1.0.0 ISO uploaded to the GitHub
release ended up with a bad on-disc signature block — the 3DO BIOS
shows the developer splash and then hands off to blackness instead
of running the tester. Rebuilding from the same source produces a
working ISO, so this point release replaces the broken artifact and
folds in two follow-up improvements that had landed on `main`.

### Highlights

- Speculative Steering-wheel driverlet bound to PBUS class `0x0F`
  with a dedicated "Wheel" row that renders eight raw button slots
  + W (steering) / T (throttle) / B (brake) axis values. Dormant
  until a wheel pod attaches; no runtime cost on existing setups.
  Byte-to-axis layout is a guess until real-hardware data comes back.
- Keyboard Caps Lock now toggles the keyboard's physical LED — a
  press flips a local flag and broadcasts
  `GENERIC_KEYBOARD_SetLEDs` over the pod chain; non-keyboard pods
  silently reject. Pause/Break (scancode `0x84`) also renders
  cleanly now that the upstream driverlet emits a synthetic press
  for the E1 sequence.

## v1.0.0 — 2026-05-14

First production release. The earlier v0.1.0 was a single-pad
`DoControlPad` polling bring-up; v1.0.0 rewrites the data path to
talk to the Portfolio event broker directly and detects / renders
every PBUS device class on the daisy chain.

### Highlights

- Per-class renderers, one row per detected pod:
  - `PAD` (0x80) — D-pad, A/B/C, X, P (Start), L/R Shift
  - `MOUSE` (0x49) — X / Y delta, L / M / R
  - `STICK` (0x01) — H / V / D analog + hat + 8 face/shift buttons
  - `GUN` (0x4D) — timing counter, line pulse, trigger
  - `ARCADE` (0xC0) — P1/P2 Coin/Start + Service
  - `KEYB` (0x02 / 0x4B) — 256-bit key matrix + a terminal-style
    typed-text row with blinking cursor and backspace
  - any other class → row labelled with the raw `0xNN` byte
- Custom event broker daemon built from the full
  [trapexit/portfolio_os](https://github.com/trapexit/portfolio_os)
  source. The stripped daemon in the devkit returns
  `ER_NotSupported` for everything past `EB_Configure` /
  `EB_EventRecord`, which kills non-pad enumeration. Our build
  links every driverlet (pad, mouse, stick, lightgun, glasses,
  keyboard) statically into the broker.
- `SillyPadDriver.c` written from scratch — no public `CPORTC0.ROM`
  ever shipped (Orbatak baked its 0xC0 driver into the game binary).
  Decodes both the PBUS-spec bit layout and the SNES23DO firmware's
  variant.
- `KeyboardDriver.c` completed from the public stub in
  portfolio_os. Implements the PS/2 Set 2 byte-stream decoder,
  populates `ked_KeyMatrix[8]`, emits all four `EVENTNUM_Keyboard*`
  events. PR open upstream.
- Bouncing-logo screensaver after 30 s idle. Cycling-colour
  silhouette on black background via an uncoded 16 bpp cel with
  pixel-buffer colour mutation per frame.

## v0.1.0 — 2026-05-12 (never tagged)

Scaffold release. Single-pad `DoControlPad` polling, raw 32-bit
bitfield render. Superseded by v1.0.0.
