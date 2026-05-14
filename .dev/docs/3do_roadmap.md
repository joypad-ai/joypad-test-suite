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

- **3DO Mouse** — X/Y movement, three buttons (L/M/R). PBUS class 0x49.
- **3DO Flightstick** — H/V/D analog axes + 12 buttons. PBUS sig 0x01 0x7B 0x08.
- **Light Gun** — counter-based aiming + trigger. PBUS class 0x4D.
- **SillyPad / Arcade (Orbatak)** — coin / start / service buttons. PBUS class 0xC0.
- **Steering wheel** — niche, if anyone actually shipped one.

Authoritative protocol docs live at
https://3dodev.com/documentation/hardware/opera/pbus and mirrored at
https://github.com/joypad-ai/joypad-os/blob/main/docs/protocols/3DO_PBUS.md.

**Status: blocked upstream on trapexit/3do-devkit.** Investigation in
v0.3 established the precise nature of the block:

### What we proved works
- Pads work via `InitEventUtility(N, M, LC_NoSeeUm|LC_Observer)` +
  `GetControlPad(N)` polling. This path uses a built-in kernel pad
  driverlet and bypasses the event broker entirely. Confirmed on
  real hardware (Panasonic FZ-1 ODE).

### What we proved is blocked
- `EB_Configure` listener subscription with the right trigger mask
  (every state-change bit + every `*DataArrived` heartbeat bit for
  Mouse / Stick / LightGun): accepted (`ACK Y`), but never delivers
  any EventRecord on real hardware (`EVT 0` with a flightstick
  physically connected and being actively manipulated).
- `EB_DescribePods` query: replies cleanly with `pod_count = 0`
  even with a flightstick + pad chain physically connected.
- `LoadProgram("System/Drivers/cport1.rom")` (and `41/49/4d.rom`,
  with leading-slash / no-slash / `$boot/`-prefix variants): fails
  on every path. Byte inspection confirms why: the files are
  PodROM format, a 32-byte custom header (DEADBEEF-prefixed
  checksum + total-byte-count + family code) followed by a
  standard AIF. `LoadProgram` expects bare AIF and bails on the
  unrecognised prefix.
- `LoadProgram("System/Tasks/eventbroker")`: succeeds in Opera
  (where no daemon is running, so this starts one), fails on real
  hardware (where the daemon was already started during the BIOS
  `startopera` sequence and a second instance is refused). Either
  outcome leaves `POD = 0` and `EVT = 0`.

### Why this is "internal API only"
- Exhaustive search of `/opt/3do-devkit/include/3do/` and
  `/include/3dosdk/{1p2,1p3,2p5}/` finds zero public functions
  that load a PodROM file. The only mentions of "LoadDriver" are
  enum values (e.g. `POD_LoadDriver = 2` in `PodLoginLogoutPhase`)
  describing daemon-internal phases.
- `EB_Command = 7` is the most likely channel for "load driverlet
  from path X" -- but `event.h` literally says: *"For internal
  use, not specified here."*
- Inspection of strings in the compiled `System/Tasks/eventbroker`
  daemon binary shows only the program name; paths and opcode
  literals are computed at runtime or stored in non-string form.

### Confirmed by trapexit
- 2026-05-12 (initial): non-pad PBUS classes need a custom kernel-
  level pod driver.
- 2026-05-12 (followup): the SDK side IS fully supported and
  shipped with retail games -- it's just that no example shows
  how, and the devkit doesn't yet bundle one. He's adding it.
- The drivers Portfolio already ships as PodROM files cover:
  Pad (built into kernel), Mouse (`cport49.rom`), Lightgun
  (`cport4d.rom`), Stereoscopic Glasses (likely `cport41.rom`),
  Analog Stick / Flightstick (likely `cport1.rom`). Only the
  Silly Control Pad (0xC0 Arcade) needs a fresh driver written.

### When the upstream example lands
1. Use whatever loader API trapexit exposes (likely a wrapper
   around the private `EB_Command` opcode, or a new SDK helper).
2. Call it for each driverlet in our app before `broker_connect()`.
3. Our existing `apply_event_record()` already dispatches by
   `ef_EventNumber` and updates per-pod state -- should "just
   work" once events flow.
4. Re-enable `ENABLE_DIAG_STATUS` in `src/main.cpp` for the
   verification pass; flip back to 0 once stable.

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
