/*
 * mouse.c — accumulate N64 mouse deltas into running absolute
 * positions. Mirrors the pattern used by pce/src/joypad_tester.c.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include "mouse.h"

static int abs_x[JOYBUS_PORT_COUNT];
static int abs_y[JOYBUS_PORT_COUNT];

void mouse_tick(joypad_port_t port)
{
    joypad_inputs_t in = joypad_get_inputs(port);
    abs_x[port] += in.stick_x;
    abs_y[port] += in.stick_y;
}

void mouse_reset(joypad_port_t port)
{
    abs_x[port] = 0;
    abs_y[port] = 0;
}

void mouse_get(joypad_port_t port, int *out_abs_x, int *out_abs_y)
{
    if (out_abs_x) *out_abs_x = abs_x[port];
    if (out_abs_y) *out_abs_y = abs_y[port];
}
