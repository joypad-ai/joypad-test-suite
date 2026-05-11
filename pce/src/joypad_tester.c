/*
 * Joypad Tester — PC Engine
 *
 * Faithful port of dshadoff's PCE_Mouse_Test:
 *   https://github.com/dshadoff/PCE_Mouse_Test
 *
 * Copyright (c) 2022 David Shadoff
 * Copyright (c) 2026 Robert Dale Smith
 *
 * Licensed under MIT — see ../LICENSE.md.
 *
 * Probes the PCE joybus for up to five controllers (single pad on port
 * 0, or up to five through a multitap), plus the PCE mouse. Toggling
 * the I button (or right mouse button) switches between mouse-decoded
 * and raw-controller modes.
 */

#include "huc.h"

main()
{
   static int abs_x, abs_y;

   abs_x = 0;
   abs_y = 0;

   set_color(0, 0);
   set_color(1, 511);

   for (;;)
   {
      put_string("joy 0:", 2, 2);
      put_hex(joy(0), 4, 9, 2);

      put_string("joy 1:", 2, 3);
      put_hex(joy(1), 4, 9, 3);

      put_string("joy 2:", 2, 4);
      put_hex(joy(2), 4, 9, 4);

      put_string("joy 3:", 2, 5);
      put_hex(joy(3), 4, 9, 5);

      put_string("joy 4:", 2, 6);
      put_hex(joy(4), 4, 9, 6);

      put_string("mouse:", 2, 8);
      put_hex(mouse_exists(), 2, 9, 8);

      put_string("x:", 6, 9);
      put_hex(mouse_x(), 2, 9, 9);

      put_string("y:", 6, 10);
      put_hex(mouse_y(), 2, 9, 10);

      abs_x -= mouse_x();
      abs_y -= mouse_y();

      put_string("abs x:", 2, 12);
      put_hex(abs_x, 4, 9, 12);

      put_string("abs y:", 2, 13);
      put_hex(abs_y, 4, 9, 13);

      if (joytrg(0) & JOY_A) {
         if (mouse_exists()) {
            mouse_disable();
         } else {
            mouse_enable();
         }
      }

      vsync(0);
   }

   return;
}
