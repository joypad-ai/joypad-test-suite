/*
 * lib_browser.c — read-back UI for VMUICONS.VMS library saves.
 *
 * For each detected VMU, attempts to load its library file and lists
 * every entry as a scrollable row with thumbnail + name + flags.
 *
 * Actions:
 *   A — load the selected entry into the editor canvas (switches mode)
 *   B + confirm — delete the entry from its source library save
 *   X — refresh (re-scan all VMUs)
 */
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/video.h>
#include <stdio.h>
#include <string.h>

#include "lib_browser.h"
#include "browser.h"        /* push-to-editor handoff */
#include "../app.h"
#include "../ui/bfont_util.h"
#include "../library/library.h"
#include "../ports/ports.h"

#define MAX_ENTRIES_SHOWN 96

typedef struct {
    int                 port;
    int                 slot;
    int                 lib_index;     /* index into the source library */
    jt_library_entry_t  entry;          /* full copy for thumbnail + load */
} flat_entry_t;

static flat_entry_t flat[MAX_ENTRIES_SHOWN];
static int          flat_count = 0;
static int          selected = 0;
static int          scroll_top = 0;
static bool         needs_refresh = true;
static bool         confirm_delete = false;
static uint32_t     last_btns = 0;

static uint32_t aggregate_pad_buttons(void);   /* forward decl */

static void enter_mode(void)
{
    needs_refresh = true;
    selected = 0;
    scroll_top = 0;
    confirm_delete = false;
    last_btns = aggregate_pad_buttons();
}
static void leave_mode(void)  { }

static void refresh(void)
{
    flat_count = 0;
    for (int p = 0; p < JT_NUM_PORTS && flat_count < MAX_ENTRIES_SHOWN; p++) {
        for (int s = 0; s < JT_NUM_SLOTS && flat_count < MAX_ENTRIES_SHOWN; s++) {
            if (jt_ports[p].slots[s].kind != JT_SLOT_VMU) continue;
            maple_device_t *dev = maple_enum_dev(p, s + 1);
            if (!dev || !dev->valid) continue;

            jt_library_t lib;
            int rc = jt_library_load(dev, &lib);
            if (rc != 0) continue;   /* no library on this VMU */

            for (int i = 0; i < lib.entry_count && flat_count < MAX_ENTRIES_SHOWN; i++) {
                flat[flat_count].port      = p;
                flat[flat_count].slot      = s;
                flat[flat_count].lib_index = i;
                flat[flat_count].entry     = lib.entries[i];
                flat_count++;
            }
        }
    }
    needs_refresh = false;
}

static uint32_t aggregate_pad_buttons(void)
{
    uint32_t b = 0;
    for (int p = 0; p < JT_NUM_PORTS; p++)
        if (jt_ports[p].present && jt_ports[p].style == JT_STYLE_PAD)
            b |= jt_ports[p].pad.buttons;
    return b;
}

static void delete_selected(void)
{
    if (selected < 0 || selected >= flat_count) return;
    flat_entry_t *fe = &flat[selected];
    maple_device_t *dev = maple_enum_dev(fe->port, fe->slot + 1);
    if (!dev) return;
    jt_library_t lib;
    if (jt_library_load(dev, &lib) != 0) return;
    if (fe->lib_index >= lib.entry_count) return;
    /* Shift entries down. */
    for (int i = fe->lib_index; i < lib.entry_count - 1; i++) {
        lib.entries[i] = lib.entries[i + 1];
    }
    lib.entry_count--;
    jt_library_save(dev, &lib);
    needs_refresh = true;
    confirm_delete = false;
    if (selected >= flat_count - 1) selected--;
    if (selected < 0) selected = 0;
}

static void load_selected_to_editor(void)
{
    if (selected < 0 || selected >= flat_count) return;
    jt_browser_push_to_editor(&flat[selected].entry.icon);
    jt_request_mode(JT_MODE_VMU_EDITOR);
}

static void update_mode(float dt)
{
    (void)dt;
    if (needs_refresh) refresh();
    uint32_t btns = aggregate_pad_buttons();
    uint32_t edges = btns & ~last_btns;

    if (confirm_delete) {
        if (edges & CONT_A) delete_selected();
        if (edges & CONT_B) confirm_delete = false;
        last_btns = btns;
        return;
    }

    if (edges & CONT_DPAD_UP)   if (selected > 0) selected--;
    if (edges & CONT_DPAD_DOWN) if (selected < flat_count - 1) selected++;
    if (edges & CONT_X)         needs_refresh = true;
    if (edges & CONT_A)         load_selected_to_editor();
    if (edges & CONT_B)         confirm_delete = (flat_count > 0);

    const int VISIBLE = 13;
    if (selected < scroll_top) scroll_top = selected;
    if (selected >= scroll_top + VISIBLE) scroll_top = selected - VISIBLE + 1;

    last_btns = btns;
}

static void draw_thumb(int sx, int sy, const jt_icon_t *icon, int size_px)
{
    int step = JT_CANVAS_W / size_px;
    if (step < 1) step = 1;
    for (int dy = 0; dy < size_px; dy++) {
        for (int dx = 0; dx < size_px; dx++) {
            int sxp = dx * step, syp = dy * step;
            uint16_t pixel;
            if (icon->has_color_icon) {
                uint8_t idx = icon->color_indices[syp * JT_CANVAS_W + sxp] & 0x0F;
                uint8_t r, g, b, a;
                jt_palette_unpack(icon->palette[idx], &r, &g, &b, &a);
                pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                int p = syp * JT_CANVAS_W + sxp;
                bool on = (icon->mono_bits[p / 8] >> (7 - (p % 8))) & 1;
                pixel = on ? 0x0000 : 0xFFFF;
            }
            if (sx + dx < 640 && sy + dy < 480) {
                vram_s[(sy + dy) * 640 + (sx + dx)] = pixel;
            }
        }
    }
}

static void draw_mode(void)
{
    /* Track empty -> populated transition so the "No entries" placeholder
     * text doesn't ghost behind freshly-loaded list rows. */
    static bool was_empty = true;
    bool is_empty = (flat_count == 0);
    if (was_empty && !is_empty) {
        /* Clear the placeholder area + list region in one wipe. */
        for (int y = 200; y < 320; y++) {
            uint16_t *p = vram_s + y * 640;
            for (int x = 0; x < 640; x++) p[x] = 0;
        }
    }
    was_empty = is_empty;

    jt_text_centered(8, JT_COL_YELLOW, JT_COL_BLACK, "VMU Icon Library");

    if (flat_count == 0) {
        jt_text_centered(220, JT_COL_WHITE, JT_COL_BLACK,
                         "No library entries found on any VMU.");
        jt_text_centered(252, JT_COL_GREY, JT_COL_BLACK,
                         "Use Editor's Save button to add an entry.");
        jt_text_centered(284, JT_COL_GREY, JT_COL_BLACK,
                         "X: refresh");
        jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                         "Start: options menu");
        return;
    }

    int row_h = 28;
    for (int row = 0; row < 13 && scroll_top + row < flat_count; row++) {
        int idx = scroll_top + row;
        flat_entry_t *fe = &flat[idx];
        int y = 40 + row * row_h;
        uint16_t fg = (idx == selected) ? JT_COL_YELLOW : JT_COL_WHITE;
        const char *marker = (idx == selected) ? ">" : " ";
        draw_thumb(8, y - 2, &fe->entry.icon, 24);
        const char *bkup = (fe->entry.flags & JT_LIB_FLAG_BACKUP) ? "*" : " ";
        const char *rmd  = (fe->entry.flags & JT_LIB_FLAG_REALMODE) ? "R" : " ";
        jt_text(40, y, fg, JT_COL_BLACK,
                "%s %c%d %s%s %-16s",
                marker, 'A' + fe->port, fe->slot + 1,
                bkup, rmd, fe->entry.name);
    }

    if (confirm_delete) {
        const int x = 170, y = 180, w = 300, h = 96;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                vram_s[(y + j) * 640 + (x + i)] = JT_COL_BLACK;
        jt_text_centered(y + 12, JT_COL_RED, JT_COL_BLACK,
                         "Delete this library entry?");
        jt_text_centered(y + 48, JT_COL_WHITE, JT_COL_BLACK,
                         "A: confirm   B: cancel");
    }

    jt_text_centered(408, JT_COL_GREY, JT_COL_BLACK,
                     "Up/Down   A: load   B: delete   X: refresh");
    jt_text_centered(432, JT_COL_GREY, JT_COL_BLACK,
                     "* = auto-backup    R = Real Mode");
    jt_text_centered(456, JT_COL_GREEN, JT_COL_BLACK,
                     "Start: options menu");
}

const jt_mode_t jt_mode_lib_browser = {
    .name   = "VMU Icon Library",
    .enter  = enter_mode,
    .leave  = leave_mode,
    .update = update_mode,
    .draw   = draw_mode,
};
