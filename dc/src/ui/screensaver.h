/*
 * screensaver.h — idle-time bouncing logo, joypad-tester convention.
 *
 * Tracks user input across all maple ports. After ~30s of idle, hands
 * over the screen to a bouncing-text screensaver that color-cycles on
 * each wall hit (matches the gba/gcn/pce testers' behavior). Any
 * input wakes it up; main.c then runs the normal mode draw on the
 * next frame.
 */
#ifndef JT_SCREENSAVER_H
#define JT_SCREENSAVER_H

#include <stdbool.h>

void jt_screensaver_init(void);

/* Call once per frame in main.c. Resets the idle timer when any
 * connected pad / mouse / keyboard shows user input this frame. */
void jt_screensaver_tick(float dt);

/* True while the screensaver has taken over rendering. main.c should
 * skip the mode draw + cursor + menu paint while this returns true. */
bool jt_screensaver_active(void);

/* Per-frame draw call. Updates position, bounces, draws the logo
 * text at the new position. No-op if !active. */
void jt_screensaver_draw(void);

/* Returns true exactly once after the screensaver deactivates. main.c
 * uses this to clear the framebuffer and re-dirty mode-specific
 * static-UI caches so the resuming mode redraws cleanly over the
 * leftover logo pixels. */
bool jt_screensaver_consume_wake(void);

#endif
