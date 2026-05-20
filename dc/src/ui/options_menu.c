/*
 * options_menu.c — modal mode picker.
 *
 * Triggered by Start+Down on any pad. While visible, captures D-pad
 * input for navigation (A confirms, Start dismisses). Modes register
 * themselves with the registry in main.c; this file just renders the
 * list and asks main.c for a mode switch when the user confirms.
 */
#include <dc/maple/controller.h>

#include "options_menu.h"
#include "bfont_util.h"
#include "osk.h"
#include "../app.h"
#include "../ports/ports.h"
#include "../video/mode.h"

static bool     visible = false;
static int      hover   = 0;
static bool     last_dpad_down = false;
static bool     last_dpad_up   = false;
static bool     last_a         = false;
static bool     last_b         = false;
static bool     last_start     = false;
static bool     last_combo     = false;
/* Set for one frame whenever the menu closes; main.c reads this to
 * skip the mode->update call on that frame, otherwise the confirming
 * Start press leaks into the newly-active mode's input handler. */
static bool     just_closed    = false;
/* Tracks whether we've already done the one-time per-pixel dim of the
 * area surrounding the menu box on this open. Doing it once per open
 * is ~11ms; doing it every frame would blow the frame budget. */
static bool     dim_applied    = false;
/* Same trick as the OSK: only repaint the menu contents on actual
 * state change (open / hover move). Otherwise the per-frame bfont
 * passes + 83K-pixel box fill cost most of the frame budget and the
 * beam catches the partial draw -> flicker. */
static bool     menu_dirty           = true;
static int      last_drawn_hover     = -1;
/* Cooldown frames after the menu opens during which confirm/cancel
 * are ignored. Handles cases where the user's Start press for "open"
 * gets briefly re-detected as a confirm (controller bounce, fast
 * re-tap, keyboard auto-repeat, etc). 9 frames ≈ 150ms at 60Hz. */
#define OPEN_COOLDOWN_FRAMES 9
static int      open_cooldown  = 0;

/* Display order matches the jt_mode_id_t enum order. Keep them in
 * sync — selecting menu position N requests jt_mode_id_t(N). */
static const char *mode_names[JT_MODE_COUNT] = {
    [JT_MODE_TESTER]      = "Controller Tester",
    [JT_MODE_BROWSER]     = "VMU File Manager",
    [JT_MODE_VMU_EDITOR]  = "VMU Icon Editor",
    [JT_MODE_LIB_BROWSER] = "VMU Icon Library",
    [JT_MODE_ABOUT]       = "About",
};

void jt_options_menu_init(void)
{
    visible = false;
    hover = (int)jt_current_mode;
}

bool jt_options_menu_visible(void) { return visible; }
bool jt_options_menu_just_closed(void) { return just_closed; }

static bool any_pad_holds(uint32_t mask)
{
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD &&
            (jt_ports[p].pad.buttons & mask) == mask) {
            return true;
        }
    }
    return false;
}

static bool any_pad_pressed(uint32_t btn, bool *last)
{
    bool now = false;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD &&
            (jt_ports[p].pad.buttons & btn)) {
            now = true;
            break;
        }
    }
    bool edge = (now && !*last);
    *last = now;
    return edge;
}

void jt_options_menu_update(float dt)
{
    (void)dt;
    bool was_visible = visible;

    /* While the on-screen keyboard is up, Start is the OSK's "Done"
     * key — must not also trigger the options menu. */
    if (jt_osk_visible()) {
        last_combo = false;
        last_dpad_down = last_dpad_up = last_a = last_b = last_start = false;
        just_closed = false;
        return;
    }

    /* Open hotkey: in the Controller Tester, Start alone is a button
     * the user is trying to test, so we require Start+Down to open.
     * In every other mode Start is unused at rest, so Start alone is
     * enough — much cheaper to press, especially with one hand on the
     * mouse. */
    uint32_t open_mask = (jt_current_mode == JT_MODE_TESTER)
                      ? (CONT_START | CONT_DPAD_DOWN)
                      : CONT_START;

    if (!visible) {
        /* Detect press edge of the open combo. */
        bool combo_now = any_pad_holds(open_mask);
        if (combo_now && !last_combo) {
            visible = true;
            hover = (int)jt_current_mode;
            /* Mark Start as already pressed so the open-press doesn't
             * immediately re-fire as a confirm on the next frame.
             * Same for the combo so a continued hold doesn't retoggle. */
            last_start = true;
            open_cooldown = OPEN_COOLDOWN_FRAMES;
            dim_applied = false;   /* fresh dim on next draw */
            menu_dirty = true;     /* force first-frame paint */
        }
        last_combo = combo_now;
        last_dpad_down = last_dpad_up = last_a = last_b = false;
        just_closed = false;
        return;
    }
    if (open_cooldown > 0) open_cooldown--;
    /* While visible, keep last_combo synced so re-pressing the open
     * combo doesn't immediately retrigger an open after a close. */
    last_combo = any_pad_holds(open_mask);

    /* Menu nav. D-pad up/down cycles selection. A or Start confirms
     * the highlighted option (Enter on a keyboard typically maps to
     * Start in Flycast's controller emulation). B cancels and closes
     * without picking. */
    int prev_hover = hover;
    if (any_pad_pressed(CONT_DPAD_DOWN, &last_dpad_down)) {
        hover = (hover + 1) % JT_MODE_COUNT;
    }
    if (any_pad_pressed(CONT_DPAD_UP, &last_dpad_up)) {
        hover = (hover + JT_MODE_COUNT - 1) % JT_MODE_COUNT;
    }
    if (hover != prev_hover) menu_dirty = true;
    /* A confirms; B cancels. Start is intentionally NOT a confirm
     * key — it's the opener (alone in non-tester modes, Start+Down
     * in Tester) and conflating opener+confirm caused the menu to
     * "open and immediately close" when bouncing/fast presses
     * tripped the same Start signal twice. */
    bool a_edge = any_pad_pressed(CONT_A, &last_a);
    bool b_edge = any_pad_pressed(CONT_B, &last_b);
    /* Still track Start edges so opener debouncing stays in sync. */
    (void)any_pad_pressed(CONT_START, &last_start);
    if (open_cooldown == 0) {
        if (a_edge) {
            jt_request_mode((jt_mode_id_t)hover);
            visible = false;
        }
        if (b_edge) {
            visible = false;
        }
    }
    just_closed = (was_visible && !visible);
}

/* Direct-VRAM rect fill (same primitive editor/osk use). */
#include <dc/video.h>
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

/* Halve each color channel of an RGB565 pixel (50% darken). Reads
 * RGB565 in: rrrrrggggggbbbbb. */
static inline uint16_t dim_pixel(uint16_t p)
{
    uint16_t r = (p >> 11) & 0x1F;
    uint16_t g = (p >> 5)  & 0x3F;
    uint16_t b = (p)       & 0x1F;
    return (uint16_t)(((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1));
}

void jt_options_menu_draw(void)
{
    if (!visible) {
        dim_applied = false;
        last_drawn_hover = -1;
        menu_dirty = true;   /* fresh paint on next open */
        return;
    }

    /* Box height covers: title row + 5 mode rows (32px each) + a
     * 28px breathing gap + footer (24px). The previous 220 made the
     * last mode (y+168..192) collide with the footer at y+188. */
    const int x = 130, y = 110, w = 380, h = 260;

    /* Skip the menu's bfont/box redraw on idle frames so we don't
     * burn ~5ms per frame painting unchanged pixels. The dim pass
     * below is also gated separately by dim_applied (only on open).
     * Net result: each frame in steady-state menu state is a noop. */
    bool need_repaint = menu_dirty || hover != last_drawn_hover;

    /* Per-pixel dim of the area around the menu was 224K writes to
     * uncached VRAM (~45ms emulated). That hung the SH4 long enough
     * that Flycast appeared to crash. Skipped — the menu's solid
     * black box + yellow border is sufficient visual separation. */
    (void)dim_pixel;
    (void)dim_applied;

    if (need_repaint) {
        /* Solid opaque dark backdrop inside the menu box. */
        fill_rect(x, y, w, h, JT_COL_BLACK);
        /* 2px yellow border. */
        fill_rect(x, y, w, 2, JT_COL_YELLOW);
        fill_rect(x, y + h - 2, w, 2, JT_COL_YELLOW);
        fill_rect(x, y, 2, h, JT_COL_YELLOW);
        fill_rect(x + w - 2, y, 2, h, JT_COL_YELLOW);

        /* Title bar inside the box. */
        jt_text_centered(y + 6, JT_COL_YELLOW, JT_COL_BLACK, "-- OPTIONS --");
        for (int i = 0; i < JT_MODE_COUNT; i++) {
            uint16_t fg = (i == hover) ? JT_COL_YELLOW : JT_COL_WHITE;
            const char *marker = (i == hover) ? ">" : " ";
            jt_text(x + 16, y + 40 + i * 32, fg, JT_COL_BLACK,
                    "%s %s", marker, mode_names[i]);
        }
        /* Video info lives on the About page; no reason to repeat it
         * inside the options menu. */
        /* Footer hint. The menu box is 380px wide -> 31 chars max
         * before wrapping into adjacent framebuffer rows (which looks
         * like flicker). Dropping the "D-pad: nav" hint keeps it
         * comfortably inside the box. */
        jt_text(x + 16, y + h - 28, JT_COL_GREY, JT_COL_BLACK,
                "A: confirm    B: cancel");

        last_drawn_hover = hover;
        menu_dirty = false;
    }
}
