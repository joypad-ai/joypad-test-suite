/*
 * screensaver.c — bouncing silhouette-logo screensaver.
 *
 * Matches the joypad-tester convention used by gcn / gba / pce:
 *   - track idle time across all maple ports.
 *   - after ~30s with no input, clear the screen and start bouncing
 *     the Joypad logo silhouette.
 *   - the logo is a 1-bit mask (gen_logo.h, baked from
 *     assets/logo.png by buildtools/make_logo.py); each set bit gets
 *     rendered in the active cycle color, clear bits stay black.
 *   - on each wall hit, flip the relevant velocity component and
 *     advance the 7-color cycle.
 *   - any input wakes; main.c then resumes the underlying mode draw,
 *     which is responsible for repainting its own UI.
 */
#include <kos.h>
#include <dc/video.h>
#include <string.h>

#include "screensaver.h"
#include "bfont_util.h"
#include "gen_logo.h"
#include "../ports/ports.h"

/* 30 seconds at 60 fps -- matches the PCE tester's threshold. */
#define IDLE_FRAMES 1800

static int  idle_counter = 0;
static bool active = false;
static int  x, y, dx, dy;
static int  color_idx = 0;
static int  last_x = -1000;
static int  last_y = -1000;
/* Pending-wake flag: set the single frame we deactivate, consumed by
 * main.c to clear leftover logo pixels + nudge mode caches. */
static bool wake_pending = false;

/* 7-color cycle. Walls advance the index. Skips dim blues that read
 * poorly on consumer CRTs. */
static const uint16_t cycle[7] = {
    JT_COL_RED,
    JT_COL_GREEN,
    JT_COL_YELLOW,
    JT_COL_CYAN,
    JT_COL_WHITE,
    JT_RGB565(255, 128,   0),  /* orange */
    JT_RGB565(255,  80, 200),  /* hot pink */
};

void jt_screensaver_init(void)
{
    idle_counter = 0;
    active = false;
    color_idx = 0;
    /* Start in the upper-left quadrant with a velocity that puts the
     * logo on a diagonal trajectory at ~2 px/frame. */
    x = 64; y = 80;
    dx = 2;  dy = 2;
    last_x = -1000;
    last_y = -1000;
}

bool jt_screensaver_active(void) { return active; }

bool jt_screensaver_consume_wake(void)
{
    bool r = wake_pending;
    wake_pending = false;
    return r;
}

static bool any_user_input(void)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        jt_port_state_t *port = &jt_ports[p];
        if (!port->present) continue;

        if (port->style == JT_STYLE_PAD) {
            if (port->pad.buttons != 0) return true;
            int sx = port->pad.stick_x, sy = port->pad.stick_y;
            if (sx > 20 || sx < -20 || sy > 20 || sy < -20) return true;
            if (port->pad.trig_l > 20 || port->pad.trig_r > 20) return true;
        }
        if (port->style == JT_STYLE_MOUSE) {
            if (port->mouse.dx || port->mouse.dy) return true;
            if (port->mouse.buttons != 0) return true;
        }
        if (port->style == JT_STYLE_KEYBOARD) {
            for (size_t i = 0; i < sizeof(port->kbd.scancodes); i++) {
                if (port->kbd.scancodes[i]) return true;
            }
            if (port->kbd.modifiers) return true;
        }
    }
    return false;
}

/* Clear the whole screen once when we activate. 32-bit writes for
 * speed; vram_s is 16bpp so two pixels per word. */
static void clear_screen_black(void)
{
    uint32_t *p = (uint32_t *)vram_s;
    for (int i = 0; i < (640 * 480) / 2; i++) p[i] = 0;
}

/* Single-pass paint of the union of the OLD and NEW bboxes. Pixels
 * inside the new bbox get the mask-based color (lit) or black (unset).
 * Pixels inside the old bbox but outside the new bbox get black --
 * that's the exit trail.
 *
 * Doing this in one pass means each affected pixel gets exactly one
 * write per frame. There's no intermediate "cleared but not painted"
 * or "painted but old still there" state for the beam to catch, so
 * single-buffer flicker + trails go away simultaneously. */
static void blit_logo_union(int old_x, int old_y,
                            int new_x, int new_y, uint16_t color)
{
    /* Union bbox bounds. */
    int u_x0 = old_x < new_x ? old_x : new_x;
    int u_y0 = old_y < new_y ? old_y : new_y;
    int u_x1 = (old_x + LOGO_W > new_x + LOGO_W) ? (old_x + LOGO_W) : (new_x + LOGO_W);
    int u_y1 = (old_y + LOGO_H > new_y + LOGO_H) ? (old_y + LOGO_H) : (new_y + LOGO_H);

    for (int sy = u_y0; sy < u_y1; sy++) {
        if (sy < 0 || sy >= 480) continue;
        uint16_t *dst_row = vram_s + sy * 640;
        int new_row = sy - new_y;
        bool row_in_new = (new_row >= 0 && new_row < LOGO_H);
        const unsigned char *mask_row =
            row_in_new ? (logo_mask + new_row * LOGO_BYTES_PER_ROW) : NULL;
        for (int sx = u_x0; sx < u_x1; sx++) {
            if (sx < 0 || sx >= 640) continue;
            int new_col = sx - new_x;
            bool in_new = row_in_new && (new_col >= 0 && new_col < LOGO_W);
            if (in_new) {
                unsigned char byte = mask_row[new_col >> 3];
                unsigned char bit  = byte & (0x80 >> (new_col & 7));
                dst_row[sx] = bit ? color : 0;
            } else {
                /* In old bbox but outside new -- exit trail, set black. */
                dst_row[sx] = 0;
            }
        }
    }
}

/* Clear an axis-aligned rect to black. */
static void clear_rect(int x0, int y0, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    for (int row = y0; row < y1; row++) {
        uint16_t *p = vram_s + row * 640;
        for (int col = x0; col < x1; col++) p[col] = 0;
    }
}

/* Clear the L-shaped region of the OLD bbox that's not covered by the
 * NEW bbox -- the "exit trail" of pixels the logo just vacated. Called
 * AFTER the new blit lands so the beam never sees an in-between
 * "cleared but not yet repainted" state. */
static void clear_exit_trail(int old_x, int old_y, int new_x, int new_y)
{
    int dx = new_x - old_x;
    int dy = new_y - old_y;
    /* If displacement is too big (>= bbox), the bboxes don't overlap;
     * clear the full old bbox. */
    if (dx >= LOGO_W || dx <= -LOGO_W ||
        dy >= LOGO_H || dy <= -LOGO_H) {
        clear_rect(old_x, old_y, LOGO_W, LOGO_H);
        return;
    }
    /* Horizontal exit strip: leftover columns on whichever side of the
     * OLD bbox is no longer inside the NEW bbox. */
    if (dx > 0) {
        clear_rect(old_x, old_y, dx, LOGO_H);              /* left strip */
    } else if (dx < 0) {
        clear_rect(new_x + LOGO_W, old_y, -dx, LOGO_H);    /* right strip */
    }
    /* Vertical exit strip: same idea on the other axis. */
    if (dy > 0) {
        clear_rect(old_x, old_y, LOGO_W, dy);              /* top strip */
    } else if (dy < 0) {
        clear_rect(old_x, new_y + LOGO_H, LOGO_W, -dy);    /* bottom strip */
    }
}

void jt_screensaver_tick(float dt)
{
    (void)dt;
    bool input = any_user_input();

    if (input) {
        if (active) {
            active = false;
            idle_counter = 0;
            last_x = -1000;
            last_y = -1000;
            wake_pending = true;   /* main.c will clear screen + dirty modes */
        }
        idle_counter = 0;
        return;
    }

    idle_counter++;
    if (!active && idle_counter >= IDLE_FRAMES) {
        active = true;
        clear_screen_black();
        last_x = -1000;
        last_y = -1000;
    }
}

void jt_screensaver_draw(void)
{
    if (!active) return;

    /* Step + bounce. Each wall hit flips the relevant component and
     * advances the cycle index. */
    int old_x = last_x, old_y = last_y;
    x += dx;
    y += dy;
    if (x < 0) { x = 0; dx = -dx; color_idx = (color_idx + 1) % 7; }
    if (x + LOGO_W > 640) { x = 640 - LOGO_W; dx = -dx; color_idx = (color_idx + 1) % 7; }
    if (y < 0) { y = 0; dy = -dy; color_idx = (color_idx + 1) % 7; }
    if (y + LOGO_H > 480) { y = 480 - LOGO_H; dy = -dy; color_idx = (color_idx + 1) % 7; }

    /* On the very first frame after activation, the old position is
     * the sentinel (-1000). Skip union painting and just blit the new
     * bbox directly -- there's no old bbox to subtract from. */
    if (old_x <= -LOGO_W) {
        /* Treat the old position as identical to the new so the union
         * pass below collapses to just the new bbox. */
        old_x = x;
        old_y = y;
    }
    blit_logo_union(old_x, old_y, x, y, cycle[color_idx]);

    last_x = x;
    last_y = y;
}
