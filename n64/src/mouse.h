/*
 * mouse.h — per-port absolute-position tracker for N64 mice.
 *
 * The N64 Mouse reports stick_x / stick_y as per-frame DELTAS rather
 * than absolute positions. The joypad subsystem doesn't accumulate
 * for us, so we keep our own running totals and present them
 * alongside the live delta in the port row.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */
#ifndef N64_MOUSE_H
#define N64_MOUSE_H

#include <libdragon.h>

/* Call once per frame for every port whose style is
 * JOYPAD_STYLE_MOUSE. Accumulates the current frame's stick deltas
 * into the running total for that port. */
void mouse_tick(joypad_port_t port);

/* Reset the running total for a port -- e.g. when a mouse is
 * unplugged or the user wants to recenter. */
void mouse_reset(joypad_port_t port);

/* Read the running total. Caller passes its own snapshot stick
 * deltas for inline display. */
void mouse_get(joypad_port_t port, int *out_abs_x, int *out_abs_y);

#endif
