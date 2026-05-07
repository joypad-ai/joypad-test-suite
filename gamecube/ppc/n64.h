#ifndef N64_H
#define N64_H

#include <gccore.h>
#include <stdbool.h>

// N64 button mask bits (response byte 0 in high byte, byte 1 in low byte)
#define N64_BTN_A 0x8000
#define N64_BTN_B 0x4000
#define N64_BTN_Z 0x2000
#define N64_BTN_START 0x1000
#define N64_DPAD_UP 0x0800
#define N64_DPAD_DOWN 0x0400
#define N64_DPAD_LEFT 0x0200
#define N64_DPAD_RIGHT 0x0100
#define N64_BTN_L 0x0020
#define N64_BTN_R 0x0010
#define N64_BTN_CUP 0x0008
#define N64_BTN_CDOWN 0x0004
#define N64_BTN_CLEFT 0x0002
#define N64_BTN_CRIGHT 0x0001

typedef enum {
  N64_KIND_NONE,
  N64_KIND_CONTROLLER,
  N64_KIND_MOUSE,
} n64_kind_t;

typedef enum {
  N64_PAK_NONE,
  N64_PAK_UNKNOWN,
  N64_PAK_MEMORY,
  N64_PAK_RUMBLE,
  N64_PAK_TRANSFER,
  N64_PAK_BIO_SENSOR,
  N64_PAK_SNAP_STATION,
} n64_pak_t;

typedef struct {
  bool present;       // shorthand for kind != N64_KIND_NONE
  n64_kind_t kind;
  u16 buttons;
  s8 stick_x;
  s8 stick_y;
  n64_pak_t pak;
  bool rumble_active;
} N64State;

// Probe & poll all 4 SI channels; fills state[SI_MAX_CHAN].
// Channels without an N64 controller have state.present == false.
// Accessory-pak detection runs at ~1Hz on N64-detected channels.
void N64_Scan(N64State *state);

// Activate/deactivate the rumble motor on an N64 channel. No-op if the
// channel is not an N64 controller with a rumble pak.
void N64_SetRumble(int chan, bool on);

#endif
