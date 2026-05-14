# 3DO Keyboard — End-to-end implementation guide

**Audience:** developer (or agent) working on `joypad-ai/joypad-os` who is
adding a USB-to-3DO keyboard output mode to the USB23DO firmware.

**Counterpart work (already done):**

- `RobertDaleSmith/portfolio_os` branch `feature/keyboard-decoder` — completes
  the stub `src/input/KeyboardDriver.c`. PBUS-side parser is a PS/2 Set 2
  state machine; 256-bit key-matrix model; emits the four standard
  `EVENTNUM_Keyboard*` event records.
- `joypad-ai/joypad-tester` `3do/` — broker patched to statically register
  the keyboard driverlet at PBUS bytes `0x02` and `0x4B`; host renderer
  shows pressed-key count + first 4 scancodes when a keyboard pod is
  detected.

The firmware side is the **last missing piece**: an output mode that
emits PBUS bytes the 3DO keyboard driverlet will accept, fed by the USB
HID keyboard input the firmware already receives.

---

## What 3DO Company *almost* shipped

3DO planned a keyboard accessory but never released one. The framework
in Portfolio is mostly complete:

| Component | Status |
|---|---|
| `EVENTNUM_KeyboardKeyPressed=14`, `Released=15`, `Update=16`, `DataArrived=17` | defined |
| `EVENTBIT0_Keyboard*` trigger-mask bits | defined |
| `POD_IsKeyboard=0x04000000` flag | defined |
| `GENERIC_Keyboard=5` (class ordinal) | defined |
| `KeyboardEventData.ked_KeyMatrix[8]` (256-bit matrix) | defined |
| `GENERIC_KEYBOARD_SetLEDs=0` command | defined |
| `KEYBOARD_LED_NUMLOCK/CAPSLOCK/SCROLLLOCK` bits | defined |
| Original `KeyboardDriver.c` | **stub** (parse / event-append were empty / `#ifdef NOTDEF`) |

The constants in the stub driver (`0xED`, `0xF4`, `0xF5`, `0xFE`, `0xFF`,
`0xAA`, `0xF0`, `0xFD`) match **PS/2 keyboard protocol Set 2** exactly.
That's the protocol the firmware needs to emit.

## What our completed `KeyboardDriver.c` expects on the wire

PBUS frame allocation per `portfolio_os/src/input/EventBroker.c`
`BitTable[24]` (line ~376):

```
keyboard:  { 24, 16 }  // 24 bits in, 16 bits out per PBUS field
```

So **3 bytes from device → console** per field, **2 bytes from console →
device**.

### Device → console (3 bytes / frame)

| Byte | Meaning |
|---|---|
| 0 | Device-class ID byte (`0x02` or `0x4B` — broker registers a driver for either) |
| 1 | One byte of the PS/2 scancode stream (see protocol below) |
| 2 | Reserved (set to 0; not consumed) |

**Important — frame-vs-stream semantics:**

PBUS samples the bus at 60 Hz. PS/2 scancodes are a stream of bytes. The
driverlet handles this by reading byte 1 of each frame and **only
processing a byte when it differs from the previous frame's value**. So
the firmware should:

- Hold the *current* scancode byte (or `0x00` if there's no pending byte)
  in byte 1 across multiple PBUS frames until the driverlet consumes it.
- When a new event needs to go to the host, change byte 1 to the next
  byte in the sequence; the driverlet will pick it up on the next field.

This is identical to how a real PS/2 keyboard works at the wire level —
clock/data lines hold state between events.

### Console → device (2 bytes / frame)

Built by `KeyboardDriver.c`'s `PD_ConstructPodOutput`:

| Byte | Meaning |
|---|---|
| 0 | PS/2 command byte (only `0xED` SET-LEDs is currently issued) |
| 1 | Command argument byte (LED bitmask) |

LED bitmask:

| Bit | LED |
|---|---|
| `0x01` | Scroll Lock |
| `0x02` | Num Lock |
| `0x04` | Caps Lock |

The firmware should drive its USB-HID-keyboard's LEDs from this byte
when the console sends an LED-update command.

## PS/2 Set 2 protocol — what the driverlet consumes

The state machine in our completed `KeyboardDriver.c::PD_ParsePodInput`
handles these byte patterns from the keyboard:

| Byte | Meaning |
|---|---|
| `0x00` | No new event this frame (firmware should hold this until something happens) |
| `0xAA` | INITOK — keyboard handshake complete. Driverlet clears the key matrix. |
| `0xE0` | Extended prefix — next scancode byte sets bit 7 in the matrix index so right-side modifiers / nav-cluster keys don't collide with left-side ones |
| `0xF0` | Release prefix — next scancode byte clears its matrix bit (instead of setting) |
| `0xFD` | Diagnostic failure — driverlet drops in-progress prefix state |
| `0xFE` | Resend request — driverlet drops in-progress prefix state |
| `0xFF` (echo) | Reserved (PS/2 reset response; no special handling in current driverlet) |
| any other byte | PS/2 Set 2 scancode |

So a normal key tap (e.g. `A`, scancode `0x1C`) is:

```
device sends: 0x1C            → driverlet: set bit 0x1C, emit KeyPressed + Update
[user releases]
device sends: 0xF0  then  0x1C → driverlet: clear bit 0x1C, emit KeyReleased + Update
```

A right-side / extended key (e.g. right Ctrl, PS/2 `0xE0 0x14`):

```
device sends: 0xE0  then  0x14 → driverlet: set bit (0x14 | 0x80) = 0x94, emit KeyPressed
device sends: 0xE0 0xF0 0x14   → driverlet: clear bit 0x94, emit KeyReleased
```

**Auto-repeat policy:** PS/2 keyboards do hardware typematic repeat
(retransmit the same scancode every ~33 ms). Our driverlet **suppresses
the duplicate press** for a key that's already-down in the matrix — auto-
repeat should be derived from time, not from the wire. The firmware can
either implement typematic itself OR send each USB-HID press exactly
once and rely on the host app to repeat. Recommend the latter (simpler;
host apps can choose their own repeat timing).

## Mapping USB HID → PS/2 Set 2 scancodes

USB HID Keyboard Page (0x07) usage IDs need to be translated to PS/2 Set 2
scancodes. The mapping is well-known — see e.g.
[osdev.org PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard#Scan_Code_Set_2)
or the
[USB HID-to-PS/2 table in QEMU](https://github.com/qemu/qemu/blob/master/ui/input-keymap.c)
for a canonical reference.

Skeleton of the table firmware would need:

```c
// Indexed by USB HID usage ID (0x04 = A ... 0xE7 = right GUI).
// Top bit = extended (E0 prefix) flag.
static const uint16_t hid_to_ps2_set2[0xE8] = {
  [0x04] = 0x1C,  // A
  [0x05] = 0x32,  // B
  [0x06] = 0x21,  // C
  [0x07] = 0x23,  // D
  [0x08] = 0x24,  // E
  // ... full table covers ~104 keys
  [0xE0] = 0x14,        // Left Ctrl
  [0xE4] = 0x14 | 0x100, // Right Ctrl (E0 prefix flag in bit 8)
  // ...
};
```

A ready-to-paste table exists in any USB-HID keyboard project (Linux
kernel, BIOS firmwares, etc.). Use Set **2** specifically — Set 1 is for
PC XT compatibility and Set 3 is rarely supported.

## Firmware implementation outline (joypad-os)

The existing firmware already has the right scaffolding. Mirror what
`_3do_silly_report` and `_3do_mouse_report` do in
`src/native/device/3do/3do_device.h` and `3do_device.c`:

### Step 1 — Define the report struct

In `src/native/device/3do/3do_device.h`:

```c
// 3DO Keyboard Report (3 bytes / 24 bits per PBUS field).
// ID 0x02 or 0x4B; PS/2 Set 2 scancode stream in byte 1.
typedef struct {
  uint8_t id;        // 0x02 (or 0x4B alias)
  uint8_t scancode;  // current PS/2 byte (held across frames until acked)
  uint8_t reserved;  // 0
} __attribute__((packed)) _3do_keyboard_report;
```

### Step 2 — Add the output mode + accessors

Extend the `tdo_output_mode_t` enum:

```c
typedef enum {
  TDO_MODE_NORMAL = 0,
  TDO_MODE_MOUSE,
  TDO_MODE_KEYBOARD,
  TDO_MODE_SILLY,
} tdo_output_mode_t;
```

And mirror the `update_3do_silly()` pattern:

```c
_3do_keyboard_report new_3do_keyboard_report(void);
void update_3do_keyboard(_3do_keyboard_report report, uint8_t instance);
```

### Step 3 — Maintain the PS/2 scancode queue

Firmware needs a per-player ring buffer of bytes-to-send. When a USB HID
keyboard event arrives, push the corresponding PS/2 sequence:

```c
// Key down: push scancode (with optional E0 prefix first).
// Key up:  push 0xF0, then scancode (with E0 first if extended).
// LED update: post-process (driver-side); see Step 5.

static inline void kb_push(uint8_t b) { ring_push(&kb_queue[player], b); }

static void kb_on_key_down(uint16_t ps2) {
  if (ps2 & 0x100) kb_push(0xE0);
  kb_push(ps2 & 0xFF);
}
static void kb_on_key_up(uint16_t ps2) {
  if (ps2 & 0x100) kb_push(0xE0);
  kb_push(0xF0);
  kb_push(ps2 & 0xFF);
}
```

On boot, push `0xAA` once after a short delay so the host driverlet clears
its key matrix (matches a real PS/2 keyboard's self-test response).

### Step 4 — Dispatch in `update_3do_report()`

Add a new branch to the existing switch in `3do_device.c::update_3do_report()`:

```c
if (output_mode == TDO_MODE_KEYBOARD) {
  _3do_keyboard_report report = new_3do_keyboard_report();
  report.id = 0x02;                  // device-class byte
  report.scancode = ring_pop_or_hold(&kb_queue[player_index], &kb_hold[player_index]);
  report.reserved = 0;
  update_3do_keyboard(report, player_index);
  return;
}
```

`ring_pop_or_hold` pops the next queued byte if one is available;
otherwise returns `kb_hold` (which was set to the last consumed byte
the previous frame, but consumed bytes are NOT replayed — set
`kb_hold = 0x00` after the driverlet has had a frame to read it).
This matches the host expectation: byte 1 holds the current scancode
byte across frames; once a new one is queued it replaces it.

The cleanest pattern: each frame, if the queue is non-empty, pop one
byte and store it in `kb_hold`. Send `kb_hold` for the next frame. On
the frame after, if no new byte is queued, send `0x00`.

### Step 5 — Handle inbound LED commands

The console issues an LED state via `GENERIC_KEYBOARD_SetLEDs`. That
arrives as a PBUS output frame — bytes `[0xED, LED_BITS]`. The
USB23DO firmware's input sampler already reads outbound bytes from the
console (the same path that gets `bg/$tasks/eventbroker` data). Tap that
flow: when a 2-byte frame with `byte[0] == 0xED` arrives, push the
`LED_BITS` byte to the USB HID keyboard via `tuh_hid_set_report` with the
output-report ID for keyboard LEDs.

```c
if (rx_byte0 == 0xED) {
  uint8_t led = rx_byte1 & (KEYBOARD_LED_SCROLLLOCK | KEYBOARD_LED_NUMLOCK | KEYBOARD_LED_CAPSLOCK);
  // Translate to USB HID LED bitmask (Num=0x01, Caps=0x02, Scroll=0x04 in HID).
  uint8_t hid_led = ...;
  tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, &hid_led, 1);
}
```

### Step 6 — Mode-switch hotkey

Add `TDO_MODE_KEYBOARD` to the `tdo_toggle_output_mode()` cycle.
Suggested cycle: NORMAL → MOUSE → KEYBOARD → SILLY → NORMAL.

## Test plan

1. **Detection** — boot the [`joypad-ai/joypad-tester`](https://github.com/joypad-ai/joypad-tester) ROM on a real 3DO with the USB23DO in keyboard mode. Confirm the Type column shows `Keyb` and `pod_Flags` includes `0x04000000` (`POD_IsKeyboard`).
2. **Scancodes** — press individual keys and confirm the row's "first 4 scancodes" column updates with the matching PS/2 Set 2 values (e.g. `A → 1C`, `Enter → 5A`, `Space → 29`).
3. **Modifiers** — press Left+Right Shift simultaneously; both should be in the active-scancode list (left = `0x12`, right = `0x59`).
4. **Extended keys** — press right Ctrl; should appear as `0x94` (= `0x14 | 0x80`) in the matrix, *not* as `0x14` (which is left Ctrl).
5. **Release** — release any key; tester's pressed-key count should decrement; scancode disappears.
6. **Auto-repeat suppression** — hold a key; pressed-key count stays at 1 (not incrementing each frame).
7. **INITOK** — power-cycle the adapter; tester should briefly show 0 pressed keys (driver matrix reset by `0xAA`).
8. **LEDs** — write a small 3DO test using `SendEvent(... SetLEDs ... 0x07)`; confirm CapsLock/NumLock/ScrollLock LEDs all light up on the USB keyboard.

## References

- 3DO Player Bus protocol: <https://github.com/joypad-ai/joypad-os/blob/main/docs/protocols/3DO_PBUS.md>
- Portfolio source (forked): `RobertDaleSmith/portfolio_os` branch `feature/keyboard-decoder`
  - `src/input/KeyboardDriver.c` — host-side parser (the consumer of what we're building here)
  - `src/input/includes/event.h` — `KeyboardEventData`, `EVENTNUM_Keyboard*`, `POD_IsKeyboard`, `GENERIC_KEYBOARD_SetLEDs`, LED bit constants
  - `src/input/EventBroker.c` — `BitTable[24]` confirms 24 in / 16 out frame allocation
- PS/2 Set 2 scancodes: <https://wiki.osdev.org/PS/2_Keyboard#Scan_Code_Set_2>
- USB HID → PS/2 mapping (canonical): QEMU's `ui/input-keymap.c` covers the full HID usage page 0x07
- Existing usb23do output modes (mouse/silly/joystick) as code-pattern reference:
  - `joypad-os/src/native/device/3do/3do_device.h` — report-struct definitions
  - `joypad-os/src/native/device/3do/3do_device.c::update_3do_report()` — dispatch
