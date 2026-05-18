/*
 * screensaver.h — idle-detect + bouncing-logo renderer for the N64
 * joypad tester. Matches the GBA / PC Engine / GameCube screensavers:
 * after ~5 s of zero input across all ports, switch from console text
 * to direct framebuffer rendering and bounce a coloured rectangle
 * across the screen, advancing through a 7-colour cycle on each wall
 * hit.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */
#ifndef N64_SCREENSAVER_H
#define N64_SCREENSAVER_H

#include <stdbool.h>

/* Increments every frame that no input is observed. main.c resets to
 * zero whenever any pad reports a pressed button or non-trivial
 * stick deflection. */
void screensaver_idle_tick(bool any_input);

/* True if the idle counter has crossed the threshold; main.c uses
 * this to decide between console rendering and framebuffer takeover. */
bool screensaver_active(void);

/* Reset the idle counter immediately (e.g. user pressed something
 * meaningful while we were already in screensaver). main.c calls
 * this on the exit transition to ensure the next frame is text. */
void screensaver_wake(void);

/* Render one frame of the bouncing logo to the next display surface.
 * Owns the display_get / display_show pair internally; the caller
 * must NOT also be calling console_render this frame. */
void screensaver_render(void);

#endif
