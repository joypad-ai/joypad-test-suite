# 3DO roadmap (v0.2 -> v1.0)

`3do/` v0.1.0 is the bring-up baseline: scaffold + Docker build +
single-pad P1 button readout. v1.0 = feature parity with the other
consoles in the suite.

## Reference: Charles Doty's 3DO Controller Test Multi

[Source on 3dodev.com](https://3dodev.com/software/homebrew/console_tools),
mirrored locally at `~/Downloads/3DOControllerTestMulti/`. Charles
Doty (RasterSoft, 2016) released this "free of any licences, credit
would be appreciated but not required" — so we can crib its patterns
or vendor selected modules directly with credit.

Specific reusable bits in his tree:
- `src/System.c` — 8-pad `InitControlPad(8)` + `readControlPad` loop
- `src/Sprite.c/.h` — pure-C CEL sprite renderer (don't need to
  port back from trapexit's C++ `BasicDisplay` if we want lean C)
- `src/Display.c` — screen-buffer + double-buffer setup
- `Banner.bmp` + `Graphics/` — example art layout (the actual art
  is RasterSoft branding, not for reuse, but the asset shape is)

When we tackle items below, the first move is "look at Charles
Doty's source for the device pattern, then build our own."

## Pending work (queued for v0.2+)

### 1. Multi-controller support (up to 8 pads)

3DO supports up to 8 control pads via the daisy-chain protocol. Call
`InitControlPad(8)` and render P1..P8 readouts. Render layout needs a
rethink — the current single-screen P1-only layout doesn't scale.

### 2. Device-type detection + parsed values

Beyond the standard control pad, the 3DO supports:

- **3DO Mouse** — X/Y movement, three buttons (L/M/R). API: separate
  from `DoControlPad`; uses the event broker. Mouse-event packets
  carry deltas and button mask. See https://3dodev.com/ docs on
  ControlPad events and event-broker mouse-class IDs.
- **Light Gun** — for games like Mad Dog McCree, Crime Patrol. Lives
  on the same daisy-chain electrical interface but reports
  (x, y, trigger) per event poll.
- **SillyPad / arcade controllers** — variants that map to standard
  pad bits but expose additional pins.
- **Steering wheel** (Capcom CPS Changer-style) — niche, if anyone
  actually shipped one.

Per-port detection: probe each daisy-chain slot for its device-class
ID and select the renderer accordingly. Display device-class label
next to "P1/P2/.." (e.g., "P1: Mouse  x +12 y -8  L M R").

Authoritative device-type docs live at https://3dodev.com/ — link to
specific page(s) in the per-port renderer comment headers.

### 3. Logo header on main screen

Match the GameCube tester's top-of-screen branded layout: small
joypad logo to the left of the "Joypad Tester - 3DO" title, rendered
as a sprite/cel. Asset comes from `joypad/assets/logo_solid.svg`
(or a 3DO-friendly resized derivative), converted to 3DO's CEL
format via the devkit's `pcxtocel` tool (or equivalent).

### 4. Custom boot/splash screen

Replace the devkit's default banner.png with our own Joypad-branded
splash. The 3DO BIOS reads the splash from the disc's TAKEME/banner
area; bake-in is handled by the devkit's `3doiso` step. Source
asset: `joypad/assets/` (same as other consoles), exported as a
3DO-friendly 320x240 image.

### 5. Bouncing-logo idle screensaver

Identical to gcn/gba/pce: after 30s idle, clear screen and bounce
the 64x64 joypad logo around the visible area, color-cycling through
red -> green -> yellow -> blue -> magenta -> cyan -> white on each
wall bounce, same step rate as the other consoles. Implementation
uses the devkit's cel/sprite API + a small palette-mutate loop.

### 6. Public release

Tag `3do-v1.0.0` once items 1-5 land and the result is verified on:

- RetroArch with Opera core (4DO emulation)
- Real 3DO hardware (CD-R burn) or an ODE

The release workflow + tag-prefix wiring is already in place
(.github/workflows/release.yml has the `3do)` parse case + build +
stage steps); just need the VERSION bump + tag push when ready.
