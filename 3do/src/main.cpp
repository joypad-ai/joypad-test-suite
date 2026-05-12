/*
 * Joypad Tester - 3DO
 *
 * Reads the active 3DO control pad each frame and renders the raw
 * 32-bit button bitfield plus per-button held labels. Up to two pads
 * supported via the 3DO's daisy-chain protocol.
 *
 * Built against trapexit/3do-devkit (pinned commit lives in
 * buildtools/Dockerfile). main.cpp gets overlaid on the devkit's
 * src/main.cpp at build time; abort.c / display.cpp helpers come
 * verbatim from the devkit, so the LaunchMe links the same way as
 * any other devkit-built homebrew.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * Licensed under MIT - see ../LICENSE.md.
 */

#include "abort.h"
#include "display.hpp"

#include "controlpad.h"
#include "event.h"
#include "types.h"

static const struct
{
  uint32      mask;
  const char *label;
} BUTTONS[] = {
  { ControlUp,         "Up"    },
  { ControlDown,       "Down"  },
  { ControlLeft,       "Left"  },
  { ControlRight,      "Right" },
  { ControlA,          "A"     },
  { ControlB,          "B"     },
  { ControlC,          "C"     },
  { ControlStart,      "Start" },
  { ControlX,          "Stop"  },
  { ControlLeftShift,  "L"     },
  { ControlRightShift, "R"     },
};
static const int BUTTON_COUNT = sizeof (BUTTONS) / sizeof (BUTTONS[0]);

static void
format_hex32 (char *out, uint32 value)
{
  static const char hex[] = "0123456789ABCDEF";
  for (int i = 0; i < 8; i++)
    out[i] = hex[(value >> ((7 - i) * 4)) & 0xF];
  out[8] = 0;
}

int
main (int argc_, char *argv_)
{
  (void)argc_;
  (void)argv_;

  BasicDisplay display;
  Err err;

  // The 3DO can chain up to 8 control pads, but Opera typically
  // emulates 1-2. InitControlPad(2) covers both single-pad and
  // chained-second-pad setups; pads beyond N are ignored.
  err = InitControlPad (2);
  if (err < 0)
    abort_err (err);

  while (true)
    {
      uint32 p1 = 0;
      uint32 p2 = 0;
      char   hex[9];
      int    y;

      // continuousBits=0xFFFFFFFF -> every button bit is treated as
      // "report held state" rather than edge-triggered. We want the
      // live held state for the on-screen indicators.
      DoControlPad (1, &p1, 0xFFFFFFFF);
      DoControlPad (2, &p2, 0xFFFFFFFF);

      display.clear ();

      display.draw_text8 (48, 16, "Joypad Tester - 3DO");
      display.draw_text8 (48, 28, "===================");

      format_hex32 (hex, p1);
      display.draw_text8 (48, 56, "P1 raw: 0x");
      display.draw_text8 (138, 56, hex);

      format_hex32 (hex, p2);
      display.draw_text8 (48, 72, "P2 raw: 0x");
      display.draw_text8 (138, 72, hex);

      // P1 per-button list: held = label, released = dot. Two-column
      // grid so the full set fits in a single screen height.
      display.draw_text8 (48, 100, "P1 buttons:");
      y = 116;
      for (int i = 0; i < BUTTON_COUNT; i++)
        {
          const int col = (i & 1) ? 140 : 48;
          const int row = y + (i / 2) * 12;
          if (p1 & BUTTONS[i].mask)
            display.draw_text8 (col, row, BUTTONS[i].label);
          else
            display.draw_text8 (col, row, ".");
        }

      display.display_and_swap ();
      display.waitvbl ();
    }

  KillControlPad ();
  return 0;
}
