/*
 * vmu_editor.c — two-pane VMU icon editor matching the web app's UX.
 *
 * Layout (640x480, 6x canvas zoom -> 192px square each):
 *   +-- y=8 ---- title bar -------------------------------------+
 *   |  COLOR (left, x=20)            MONO (right, x=240)        |
 *   |   canvas 192x192               canvas 192x192             |
 *   |   palette 4x4 swatches         palette 4x4 toggle states  |
 *   |   [Reset]                      [Reset] [Invert]           |
 *   +-- y=400 status / hints / Real Mode flag --------------------+
 *
 * Two canvases visible at once. The "active pane" is whichever the
 * cursor is hovering over: A paints in that pane, B erases. Color
 * pane: paint = current_color; mono pane: paint = on, erase = off.
 *
 * Mono palette panel: 16 toggle cells, one per color-palette index.
 * Click a cell -> jt_canvas_mono_toggle_palette() flips every mono
 * pixel of that color. That's the color->silhouette translation
 * shortcut from the web app.
 *
 * Reset / Invert buttons live below their respective panes.
 */
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/mouse.h>
#include <dc/biosfont.h>
#include <dc/video.h>
#include <stdio.h>
#include <string.h>

#include "vmu_editor.h"
#include "browser.h"
#include "../ui/bfont_util.h"
#include "../ui/osk.h"
#include "../canvas/canvas.h"
#include "../vms/apply.h"
#include "../library/library.h"
#include "../ports/ports.h"
#include <time.h>
#include "../input/cursor.h"
#include "../video/mode.h"

/* From browser.c -- pulls any pending icon the user just extracted. */
bool jt_browser_consume_pending(jt_icon_t *out);

#define ZOOM         6                          /* 32 * 6 = 192 */
#define PANE_SIZE    (JT_CANVAS_W * ZOOM)       /* 192 */

#define COLOR_X      24
#define MONO_X       240
#define CANVAS_Y     40

#define COLOR_PAL_Y  (CANVAS_Y + PANE_SIZE + 12)
#define MONO_PAL_Y   COLOR_PAL_Y
#define BUTTON_Y     (COLOR_PAL_Y + 4 * 22 + 12)

#define SWATCH_W     22

static jt_canvas_t canvas;
static bool initialized = false;

static uint32_t last_btns = 0;
static bool     last_action_button = false;     /* for stroke-undo */

/* Apply / Save state. Both verbs use the same VMU picker overlay;
 * picker_purpose distinguishes the action to perform on confirm. */
typedef enum { PICK_NONE = 0, PICK_APPLY, PICK_SAVE } picker_purpose_t;
static picker_purpose_t picker_purpose = PICK_NONE;
static int           picker_idx = 0;
static char          apply_result_text[80] = {0};
static unsigned      apply_result_frames = 0;
static int           apply_target_count = 0;
static struct { int port, slot; } apply_targets[JT_NUM_PORTS * JT_NUM_SLOTS];

/* While Save is in flight after VMU pick, we open the OSK for the
 * entry name. These remember which VMU to write to once the name comes
 * back. */
static int  pending_save_port = -1;
static int  pending_save_slot = -1;
static bool save_awaiting_name = false;

static void rebuild_apply_targets(void)
{
    apply_target_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        for (int s = 0; s < JT_NUM_SLOTS; s++) {
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) {
                apply_targets[apply_target_count].port = p;
                apply_targets[apply_target_count].slot = s;
                apply_target_count++;
            }
        }
    }
    if (picker_idx >= apply_target_count) picker_idx = 0;
}

/* Write canvas to the library save on a VMU. */
static void save_to_library(int port, int slot, const char *entry_name)
{
    maple_device_t *dev = maple_enum_dev(port, slot + 1);
    if (!dev || !dev->valid) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: VMU disconnected", 'A' + port, slot + 1);
        apply_result_frames = 180;
        return;
    }
    jt_library_t lib;
    int rc = jt_library_load(dev, &lib);
    if (rc == -1) {
        jt_library_init(&lib);
    } else if (rc < 0) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: Library load failed (%d)", 'A' + port, slot + 1, rc);
        apply_result_frames = 180;
        return;
    }
    jt_library_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    const char *name = (entry_name && entry_name[0]) ? entry_name : "Untitled";
    strncpy(entry.name, name, JT_LIBRARY_NAME_LEN);
    strncpy(entry.description, canvas.description, JT_LIBRARY_DESC_LEN);
    entry.timestamp = (uint64_t)time(NULL);
    entry.flags = JT_LIB_FLAG_COLOR | JT_LIB_FLAG_MONO;
    if (canvas.real_mode_flag) entry.flags |= JT_LIB_FLAG_REALMODE;
    jt_canvas_to_icon(&canvas, &entry.icon);

    if (jt_library_append(&lib, &entry) != 0) {
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "%c%d: Library full (cap %d)",
                 'A' + port, slot + 1, lib.capacity);
        apply_result_frames = 180;
        return;
    }
    int wrc = jt_library_save(dev, &lib);
    snprintf(apply_result_text, sizeof(apply_result_text),
             "%c%d: %s",
             'A' + port, slot + 1,
             (wrc == 0) ? "Saved to library" : "Library write failed");
    apply_result_frames = 180;
}

static void editor_enter(void)
{
    if (!initialized) {
        jt_canvas_init(&canvas);
        initialized = true;
    }
    jt_icon_t loaded;
    if (jt_browser_consume_pending(&loaded)) {
        jt_canvas_from_icon(&canvas, &loaded);
        jt_canvas_mono_sync_palette(&canvas);
    }
}

static void editor_leave(void) { }

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++) {
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD) {
            b |= jt_ports[p].pad.buttons;
        }
    }
    return b;
}

/* Region hit-test. Returns the kind of region under (x,y). */
typedef enum {
    R_NONE = 0,
    R_COLOR_CANVAS,
    R_MONO_CANVAS,
    R_COLOR_SWATCH,    /* arg = palette index */
    R_MONO_TOGGLE,     /* arg = palette index */
    R_BTN_COLOR_RESET,
    R_BTN_MONO_RESET,
    R_BTN_MONO_INVERT,
    R_BTN_REALMODE,
    R_BTN_APPLY,
    R_BTN_SAVE,
    R_BTN_NAME,
} region_t;

static region_t region_at(int x, int y, int *cx, int *cy, int *arg)
{
    if (cx) *cx = 0;
    if (cy) *cy = 0;
    if (arg) *arg = 0;

    /* Canvases. */
    if (x >= COLOR_X && x < COLOR_X + PANE_SIZE &&
        y >= CANVAS_Y && y < CANVAS_Y + PANE_SIZE) {
        if (cx) *cx = (x - COLOR_X) / ZOOM;
        if (cy) *cy = (y - CANVAS_Y) / ZOOM;
        return R_COLOR_CANVAS;
    }
    if (x >= MONO_X && x < MONO_X + PANE_SIZE &&
        y >= CANVAS_Y && y < CANVAS_Y + PANE_SIZE) {
        if (cx) *cx = (x - MONO_X) / ZOOM;
        if (cy) *cy = (y - CANVAS_Y) / ZOOM;
        return R_MONO_CANVAS;
    }
    /* Color palette strip — 4 rows x 4 cols at x=COLOR_X. */
    if (x >= COLOR_X && x < COLOR_X + 4 * SWATCH_W &&
        y >= COLOR_PAL_Y && y < COLOR_PAL_Y + 4 * SWATCH_W) {
        int row = (y - COLOR_PAL_Y) / SWATCH_W;
        int col = (x - COLOR_X) / SWATCH_W;
        if (arg) *arg = row * 4 + col;
        return R_COLOR_SWATCH;
    }
    /* Mono toggle strip. */
    if (x >= MONO_X && x < MONO_X + 4 * SWATCH_W &&
        y >= MONO_PAL_Y && y < MONO_PAL_Y + 4 * SWATCH_W) {
        int row = (y - MONO_PAL_Y) / SWATCH_W;
        int col = (x - MONO_X) / SWATCH_W;
        if (arg) *arg = row * 4 + col;
        return R_MONO_TOGGLE;
    }
    /* Buttons. */
    if (y >= BUTTON_Y && y < BUTTON_Y + 24) {
        if (x >= COLOR_X && x < COLOR_X + 60)              return R_BTN_COLOR_RESET;
        if (x >= MONO_X && x < MONO_X + 60)                return R_BTN_MONO_RESET;
        if (x >= MONO_X + 72 && x < MONO_X + 144)          return R_BTN_MONO_INVERT;
    }
    if (y >= BUTTON_Y + 28 && y < BUTTON_Y + 52) {
        if (x >= COLOR_X && x < COLOR_X + 120)             return R_BTN_REALMODE;
        if (x >= COLOR_X + 132 && x < COLOR_X + 192)       return R_BTN_APPLY;
        if (x >= COLOR_X + 204 && x < COLOR_X + 264)       return R_BTN_SAVE;
        if (x >= COLOR_X + 276 && x < COLOR_X + 336)       return R_BTN_NAME;
    }
    return R_NONE;
}

static void open_picker(picker_purpose_t purpose)
{
    rebuild_apply_targets();
    if (apply_target_count == 0) {
        const char *verb = (purpose == PICK_SAVE) ? "save to library" : "Apply";
        snprintf(apply_result_text, sizeof(apply_result_text),
                 "No VMU detected -- plug one in to %s.", verb);
        apply_result_frames = 180;
        picker_purpose = PICK_NONE;
    } else {
        picker_purpose = purpose;
    }
}

static void editor_update(float dt)
{
    (void)dt;

    /* OSK takes input precedence. */
    if (jt_osk_visible()) {
        jt_osk_update(dt);
        char buf[JT_OSK_MAX_LEN + 1];
        if (jt_osk_consume_text(buf, sizeof(buf))) {
            if (save_awaiting_name) {
                /* Save flow: write entry to library. */
                save_to_library(pending_save_port, pending_save_slot, buf);
                save_awaiting_name = false;
                pending_save_port = -1;
                pending_save_slot = -1;
            } else {
                /* Plain naming: update description. */
                strncpy(canvas.description, buf, sizeof(canvas.description) - 1);
                canvas.description[sizeof(canvas.description) - 1] = '\0';
            }
        }
        return;
    }

    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    if (picker_purpose != PICK_NONE) {
        if (edges & CONT_DPAD_UP)   if (picker_idx > 0) picker_idx--;
        if (edges & CONT_DPAD_DOWN) if (picker_idx < apply_target_count - 1) picker_idx++;
        if ((edges & CONT_A) && apply_target_count > 0) {
            int port = apply_targets[picker_idx].port;
            int slot = apply_targets[picker_idx].slot;
            if (picker_purpose == PICK_APPLY) {
                jt_icon_t icon;
                jt_canvas_to_icon(&canvas, &icon);
                jt_apply_result_t r = jt_apply_icondata(&icon, port, slot);
                snprintf(apply_result_text, sizeof(apply_result_text),
                         "%c%d: %s", 'A' + port, slot + 1, jt_apply_result_str(r));
                apply_result_frames = 180;
            } else if (picker_purpose == PICK_SAVE) {
                /* Defer the actual write until the user types a name. */
                pending_save_port = port;
                pending_save_slot = slot;
                save_awaiting_name = true;
                jt_osk_begin("Library entry name", "", JT_LIBRARY_NAME_LEN);
            }
            picker_purpose = PICK_NONE;
        }
        if (edges & CONT_B) picker_purpose = PICK_NONE;
        last_btns = btns;
        return;
    }

    if (edges & CONT_START) {
        open_picker(PICK_APPLY);
        last_btns = btns;
        return;
    }

    /* Y cycles tool; X picks (on canvas). */
    if (edges & CONT_Y) {
        canvas.tool = (jt_tool_t)((canvas.tool + 1) % JT_TOOL_COUNT);
    }

    /* D-pad up/down still cycles palette index for paint. */
    if (edges & CONT_DPAD_UP)   canvas.current_color = (canvas.current_color + JT_PALETTE_ENTRIES - 1) & 0x0F;
    if (edges & CONT_DPAD_DOWN) canvas.current_color = (canvas.current_color + 1) & 0x0F;

    /* Determine cursor region + apply paint / button actions. */
    bool a_now = (btns & CONT_A) || jt_cursor.button_a;
    bool b_now = (btns & CONT_B) || jt_cursor.button_b;
    bool a_edge = a_now && !last_action_button;

    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);

    if (r == R_COLOR_CANVAS) {
        canvas.layer = JT_LAYER_COLOR;
        if (a_now) {
            if (a_edge) jt_canvas_push_undo(&canvas);
            if (canvas.tool == JT_TOOL_FILL && a_edge) {
                jt_canvas_fill(&canvas, cx, cy);
            } else if (canvas.tool == JT_TOOL_PICK && a_edge) {
                jt_canvas_pick(&canvas, cx, cy);
            } else {
                jt_canvas_set_pixel(&canvas, cx, cy);
            }
        } else if (b_now) {
            if (!last_action_button) jt_canvas_push_undo(&canvas);
            jt_canvas_erase_pixel(&canvas, cx, cy);
        } else if (edges & CONT_X) {
            jt_canvas_pick(&canvas, cx, cy);
        }
    } else if (r == R_MONO_CANVAS) {
        canvas.layer = JT_LAYER_MONO;
        if (a_now) {
            if (a_edge) jt_canvas_push_undo(&canvas);
            jt_canvas_mono_set(&canvas, cx, cy, true);
            jt_canvas_mono_sync_palette(&canvas);
        } else if (b_now) {
            if (!last_action_button) jt_canvas_push_undo(&canvas);
            jt_canvas_mono_set(&canvas, cx, cy, false);
            jt_canvas_mono_sync_palette(&canvas);
        }
    } else if (r == R_COLOR_SWATCH && a_edge) {
        canvas.current_color = (uint8_t)(arg & 0x0F);
    } else if (r == R_MONO_TOGGLE && a_edge) {
        jt_canvas_mono_toggle_palette(&canvas, arg);
    } else if (a_edge) {
        switch (r) {
            case R_BTN_COLOR_RESET:  jt_canvas_color_reset(&canvas); break;
            case R_BTN_MONO_RESET:   jt_canvas_mono_reset(&canvas);  break;
            case R_BTN_MONO_INVERT:  jt_canvas_mono_invert(&canvas); break;
            case R_BTN_REALMODE:     canvas.real_mode_flag = !canvas.real_mode_flag; break;
            case R_BTN_APPLY:        open_picker(PICK_APPLY);        break;
            case R_BTN_SAVE:         open_picker(PICK_SAVE);         break;
            case R_BTN_NAME:
                jt_osk_begin("Description (16 chars max)",
                             canvas.description, 16);
                break;
            default: break;
        }
    }

    /* Z-style hotkey for Real Mode toggle from pad-only users: hold
     * Start when not over Apply button? Use D-pad LEFT/RIGHT (free now). */
    if (edges & CONT_DPAD_LEFT)  canvas.real_mode_flag = !canvas.real_mode_flag;
    if (edges & CONT_DPAD_RIGHT) jt_osk_begin("Description (16 chars max)",
                                              canvas.description, 16);

    last_btns = btns;
    last_action_button = a_now;
    if (apply_result_frames > 0) apply_result_frames--;
}

/* Direct framebuffer rect fill (same as v0.2.0 helper). */
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || x + w > 640 || y + h > 480) return;
    for (int j = 0; j < h; j++) {
        uint16_t *row = vram_s + (y + j) * 640 + x;
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

static void draw_canvases(void)
{
    /* Color pane. */
    for (int y = 0; y < JT_CANVAS_H; y++) {
        for (int x = 0; x < JT_CANVAS_W; x++) {
            uint8_t idx = canvas.color_indices[y * JT_CANVAS_W + x] & 0x0F;
            uint8_t r, g, b, a;
            jt_palette_unpack(canvas.palette[idx], &r, &g, &b, &a);
            uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            fill_rect(COLOR_X + x * ZOOM, CANVAS_Y + y * ZOOM, ZOOM, ZOOM, rgb565);
        }
    }
    /* Mono pane. */
    for (int y = 0; y < JT_CANVAS_H; y++) {
        for (int x = 0; x < JT_CANVAS_W; x++) {
            int p = y * JT_CANVAS_W + x;
            bool on = (canvas.mono_bits[p / 8] >> (7 - (p % 8))) & 1;
            uint16_t col = on ? JT_RGB565(29, 71, 129) : JT_RGB565(138, 248, 219);
            fill_rect(MONO_X + x * ZOOM, CANVAS_Y + y * ZOOM, ZOOM, ZOOM, col);
        }
    }
    /* 2px borders. */
    fill_rect(COLOR_X - 2, CANVAS_Y - 2, PANE_SIZE + 4, 2, JT_COL_YELLOW);
    fill_rect(COLOR_X - 2, CANVAS_Y + PANE_SIZE, PANE_SIZE + 4, 2, JT_COL_YELLOW);
    fill_rect(COLOR_X - 2, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_YELLOW);
    fill_rect(COLOR_X + PANE_SIZE, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_YELLOW);
    fill_rect(MONO_X - 2, CANVAS_Y - 2, PANE_SIZE + 4, 2, JT_COL_WHITE);
    fill_rect(MONO_X - 2, CANVAS_Y + PANE_SIZE, PANE_SIZE + 4, 2, JT_COL_WHITE);
    fill_rect(MONO_X - 2, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_WHITE);
    fill_rect(MONO_X + PANE_SIZE, CANVAS_Y - 2, 2, PANE_SIZE + 4, JT_COL_WHITE);
}

static void draw_color_palette(void)
{
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        uint8_t r, g, b, a;
        jt_palette_unpack(canvas.palette[i], &r, &g, &b, &a);
        uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        int row = i / 4, col = i % 4;
        int sx = COLOR_X + col * SWATCH_W;
        int sy = COLOR_PAL_Y + row * SWATCH_W;
        fill_rect(sx + 1, sy + 1, SWATCH_W - 2, SWATCH_W - 2, rgb565);
        if (i == canvas.current_color) {
            fill_rect(sx - 1, sy - 1, SWATCH_W + 2, 1, JT_COL_YELLOW);
            fill_rect(sx - 1, sy + SWATCH_W, SWATCH_W + 2, 1, JT_COL_YELLOW);
            fill_rect(sx - 1, sy - 1, 1, SWATCH_W + 2, JT_COL_YELLOW);
            fill_rect(sx + SWATCH_W, sy - 1, 1, SWATCH_W + 2, JT_COL_YELLOW);
        }
    }
}

static void draw_mono_toggles(void)
{
    /* Each cell shows the corresponding color so the user knows which
     * index it maps to, with a filled black overlay if "on" in mono. */
    for (int i = 0; i < JT_PALETTE_ENTRIES; i++) {
        uint8_t r, g, b, a;
        jt_palette_unpack(canvas.palette[i], &r, &g, &b, &a);
        uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        int row = i / 4, col = i % 4;
        int sx = MONO_X + col * SWATCH_W;
        int sy = MONO_PAL_Y + row * SWATCH_W;
        fill_rect(sx + 1, sy + 1, SWATCH_W - 2, SWATCH_W - 2, rgb565);
        if (canvas.mono_palette_states[i]) {
            /* Mono-on indicator: filled cyan diamond/border. */
            fill_rect(sx + 3, sy + 3, SWATCH_W - 6, SWATCH_W - 6,
                      JT_RGB565(29, 71, 129));
        }
    }
}

static void draw_button(int x, int y, int w, const char *label, bool on)
{
    uint16_t bg = on ? JT_COL_YELLOW : JT_RGB565(60, 60, 60);
    uint16_t fg = on ? JT_COL_BLACK : JT_COL_WHITE;
    fill_rect(x, y, w, 24, bg);
    fill_rect(x, y, w, 1, JT_COL_WHITE);
    fill_rect(x, y + 23, w, 1, JT_COL_WHITE);
    fill_rect(x, y, 1, 24, JT_COL_WHITE);
    fill_rect(x + w - 1, y, 1, 24, JT_COL_WHITE);
    int tx = x + (w - (int)strlen(label) * 12) / 2;
    jt_text(tx, y + 2, fg, bg, "%s", label);
}

static void draw_buttons(void)
{
    /* Row 1: Reset / Reset / Invert. */
    draw_button(COLOR_X, BUTTON_Y, 60, "Reset", false);
    draw_button(MONO_X,  BUTTON_Y, 60, "Reset", false);
    draw_button(MONO_X + 72, BUTTON_Y, 72, "Invert", false);
    /* Row 2: Real Mode / Apply / Save / Name. */
    draw_button(COLOR_X,           BUTTON_Y + 28, 120, "Real Mode", canvas.real_mode_flag);
    draw_button(COLOR_X + 132,     BUTTON_Y + 28, 60,  "Apply", false);
    draw_button(COLOR_X + 204,     BUTTON_Y + 28, 60,  "Save",  false);
    draw_button(COLOR_X + 276,     BUTTON_Y + 28, 60,  "Name",  false);
}

static const char *tool_name(jt_tool_t t)
{
    switch (t) {
        case JT_TOOL_PAINT: return "Paint";
        case JT_TOOL_ERASE: return "Erase";
        case JT_TOOL_FILL:  return "Fill";
        case JT_TOOL_PICK:  return "Pick";
        default:            return "?";
    }
}

static void draw_status(void)
{
    /* Right-column status from x=450, y=40. */
    int x = 450, y = CANVAS_Y;
    jt_text(x, y,       JT_COL_YELLOW, JT_COL_BLACK, "Tool:  %s", tool_name(canvas.tool));
    jt_text(x, y + 24,  JT_COL_WHITE,  JT_COL_BLACK, "Color: %d", canvas.current_color);
    jt_text(x, y + 48,  JT_COL_GREY,   JT_COL_BLACK, "Undo:  %d/%d",
            canvas.undo_count, JT_UNDO_DEPTH);
    jt_text(x, y + 72,  JT_COL_GREY,   JT_COL_BLACK, "Real: %s",
            canvas.real_mode_flag ? "ON" : "off");
    jt_text(x, y + 96,  JT_COL_GREY,   JT_COL_BLACK, "Name:");
    jt_text(x, y + 120, JT_COL_CYAN,   JT_COL_BLACK, "%s",
            canvas.description[0] ? canvas.description : "(none)");

    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);
    if (r == R_COLOR_CANVAS || r == R_MONO_CANVAS) {
        jt_text(x, y + 152, JT_COL_GREEN, JT_COL_BLACK, "Px:    %02d,%02d", cx, cy);
    } else {
        jt_text(x, y + 152, JT_COL_GREY,  JT_COL_BLACK, "Px:    --,--");
    }

    int vmu_count = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        for (int s = 0; s < JT_NUM_SLOTS; s++)
            if (jt_ports[p].slots[s].kind == JT_SLOT_VMU) vmu_count++;
    jt_text(x, y + 176, JT_COL_GREEN, JT_COL_BLACK, "VMUs:  %d", vmu_count);
}

static void draw_cursor_crosshair(void)
{
    int cx, cy, arg;
    region_t r = region_at(jt_cursor.x, jt_cursor.y, &cx, &cy, &arg);
    int base_x = 0;
    if (r == R_COLOR_CANVAS) base_x = COLOR_X;
    else if (r == R_MONO_CANVAS) base_x = MONO_X;
    else return;
    int sx = base_x + cx * ZOOM;
    int sy = CANVAS_Y + cy * ZOOM;
    fill_rect(sx - 1, sy - 1, ZOOM + 2, 1, JT_COL_YELLOW);
    fill_rect(sx - 1, sy + ZOOM, ZOOM + 2, 1, JT_COL_YELLOW);
    fill_rect(sx - 1, sy - 1, 1, ZOOM + 2, JT_COL_YELLOW);
    fill_rect(sx + ZOOM, sy - 1, 1, ZOOM + 2, JT_COL_YELLOW);
}

static void draw_picker(void)
{
    if (picker_purpose == PICK_NONE) return;
    const int x = 170, y = 130;
    fill_rect(x, y, 300, 220, JT_COL_BLACK);
    fill_rect(x, y, 300, 2, JT_COL_YELLOW);
    fill_rect(x, y + 218, 300, 2, JT_COL_YELLOW);
    fill_rect(x, y, 2, 220, JT_COL_YELLOW);
    fill_rect(x + 298, y, 2, 220, JT_COL_YELLOW);
    const char *title = (picker_purpose == PICK_SAVE) ?
                        "SAVE TO LIBRARY" : "APPLY ICONDATA_VMS";
    jt_text(x + 8, y + 8, JT_COL_YELLOW, JT_COL_BLACK, "%s", title);
    if (apply_target_count == 0) {
        jt_text(x + 16, y + 40, JT_COL_RED, JT_COL_BLACK, "No VMU detected.");
    } else {
        for (int i = 0; i < apply_target_count; i++) {
            uint16_t fg = (i == picker_idx) ? JT_COL_YELLOW : JT_COL_WHITE;
            const char *m = (i == picker_idx) ? ">" : " ";
            jt_text(x + 16, y + 40 + i * 28, fg, JT_COL_BLACK,
                    "%s Port %c, Slot %d",
                    m, 'A' + apply_targets[i].port,
                    apply_targets[i].slot + 1);
        }
    }
    jt_text(x + 16, y + 188, JT_COL_GREY, JT_COL_BLACK,
            (picker_purpose == PICK_SAVE) ?
            "A: next (name)  B: cancel" : "A: write  B: cancel");
}

static void editor_draw(void)
{
    /* OSK is a full-screen modal. */
    if (jt_osk_visible()) {
        jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK, "Naming");
        jt_osk_draw();
        return;
    }

    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK, "VMU Icon Editor");

    draw_canvases();
    draw_color_palette();
    draw_mono_toggles();
    draw_buttons();
    draw_status();
    draw_cursor_crosshair();
    draw_picker();

    if (apply_result_frames > 0) {
        jt_text_centered(BUTTON_Y + 60, JT_COL_GREEN, JT_COL_BLACK,
                         "%s", apply_result_text);
    }

    jt_text_centered(444, JT_COL_GREY, JT_COL_BLACK,
                     "Cursor: A paint  B erase  X pick  Y tool  D-pad: color+/realmode");
    jt_text_centered(468, JT_COL_GREEN, JT_COL_BLACK,
                     "Hold Start+Down for options menu");
}

const jt_mode_t jt_mode_vmu_editor = {
    .name   = "VMU Icon Editor",
    .enter  = editor_enter,
    .leave  = editor_leave,
    .update = editor_update,
    .draw   = editor_draw,
};
