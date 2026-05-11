// main_tester.c — minimal GBA test ROM, used by joypad-tester (GC/Wii)
// to multiboot the GBA into a visual "I'm here, here are my buttons"
// view, and usable from a flash cart as a standalone GBA button tester.
//
// Same Doridian-style joybus handshake as main.c (the eyes variant) so
// the GameCube host can also read JOYTR = KEYINPUT and surface button
// state on its own UI. The visible difference is the on-GBA display:
// text console with the current button state, no eyes overlay.
//
// Idle screensaver: after ~30 s of zero input, switches to GBA Mode 3
// and bounces the joypad logo bitmap around the screen, cycling colour
// on each wall hit. Mirrors the joypad-tester GC screensaver — same
// 64x54 logo mask (logo_small from gen_logo.h), same 7-colour cycle
// (red, green, yellow, blue, magenta, cyan, white), same 4 px/3 px
// step at 30 Hz. Any button press exits and restores the live tester.

#include <gba_console.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_sio.h>
#include <gba_systemcalls.h>
#include <gba_video.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// JOYCNT register (NOT JOYSTAT). Bit 0 = RST flag set by master cmd 0xFF.
#define REG_JOYCNTRL    (*(volatile uint16_t*)0x4000140)
#define REG_JOYCTRL_RST 0b00000001

#define RGB5(r,g,b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

#define GBA_W 240
#define GBA_H 160

// VBlank sync via VCOUNT polling. RST checked inside the busy-poll so
// a host cmd 0xFF arriving during the ~17 ms wait triggers SystemCall
// (0x26) within microseconds.
static inline void poll_reset(void)
{
    if (REG_JOYCNTRL & REG_JOYCTRL_RST) {
        SystemCall(0x26);
        while (1) ;
    }
}

static void wait_vblank(void)
{
    while (REG_VCOUNT >= 160) poll_reset();
    while (REG_VCOUNT <  160) poll_reset();
}

static void on_vblank(void)
{
    REG_JOYTR = REG_KEYINPUT;
}

// Button labels in REG_KEYINPUT bit order. 0 = pressed (GBA convention),
// so we invert when displaying.
//   bit 0 A, 1 B, 2 Sel, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down,
//   bit 8 R, 9 L
static const char *btn_name[10] = {
    "A", "B", "Sel", "Start", "Right", "Left", "Up", "Down", "R", "L",
};

// 2-column layout: btn_name index -> (row offset, right-column flag).
//   left col   right col
//   A          Up
//   B          Down
//   Sel        Left
//   Start      Right
//   L          R
static const struct { uint8_t row, right; } btn_pos[10] = {
    {0,0}, {1,0}, {2,0}, {3,0},   // A, B, Sel, Start
    {3,1}, {2,1}, {0,1}, {1,1},   // Right, Left, Up, Down
    {4,1}, {4,0},                  // R, L
};

#define ROW0   8
#define COL_L  3
#define COL_R  16
#define IND_L  (COL_L + 7)
#define IND_R  (COL_R + 7)

// ============================================================================
// Screensaver — bitmap-mode joypad logo bouncer matching the GC version.
// ============================================================================

// 64x54 1-bit alpha mask of the joypad logo. Identical bytes to
// gen_logo.h's logo_small_mask on the GC side — same source PNG, same
// silhouette. Each row is 8 bytes, big-endian bit order (bit 7 = leftmost).
#define LOGO_W   64
#define LOGO_H   54
#define LOGO_BPR 8
static const uint8_t logo_mask[432] = {
    0x00, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x00,
    0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0,
    0x03, 0xff, 0x8f, 0xff, 0xff, 0xf1, 0xff, 0xc0, 0x03, 0xff, 0x07, 0xff, 0xff, 0xe0, 0xff, 0xc0,
    0x07, 0xfe, 0x07, 0xff, 0xff, 0xe0, 0xff, 0xc0, 0x07, 0xfe, 0x07, 0xff, 0xff, 0xe0, 0xff, 0xe0,
    0x07, 0xff, 0x07, 0xff, 0xff, 0xe0, 0xff, 0xe0, 0x07, 0xfe, 0x07, 0xff, 0xff, 0xf9, 0xff, 0xe0,
    0x0f, 0xc0, 0x00, 0x3f, 0xfc, 0x3f, 0x87, 0xe0, 0x0f, 0xc0, 0x00, 0x1f, 0xf8, 0x1f, 0x83, 0xf0,
    0x0f, 0xc0, 0x00, 0x1f, 0xf8, 0x1f, 0x03, 0xf0, 0x0f, 0xc0, 0x00, 0x1f, 0xfc, 0x1f, 0x83, 0xf0,
    0x0f, 0xe0, 0x00, 0x3f, 0xfc, 0x3f, 0xc7, 0xf0, 0x1f, 0xfe, 0x07, 0xff, 0xff, 0xf1, 0xff, 0xf8,
    0x1f, 0xff, 0x07, 0xff, 0xff, 0xe0, 0xff, 0xf8, 0x1f, 0xfe, 0x07, 0xff, 0xef, 0xe0, 0xff, 0xf8,
    0x1f, 0xff, 0x07, 0xe3, 0xc7, 0xe0, 0xff, 0xf8, 0x3f, 0xff, 0x0f, 0xe0, 0x0f, 0xf0, 0xff, 0xf8,
    0x3f, 0xff, 0xff, 0xf0, 0x0f, 0xff, 0xff, 0xfc, 0x3f, 0xff, 0xff, 0xfc, 0x3f, 0xff, 0xff, 0xfc,
    0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
    0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xff, 0xfc, 0x00, 0x00, 0x7f, 0xff, 0xfe,
    0xff, 0xff, 0xf0, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 0x07, 0xff, 0xff,
    0xff, 0xff, 0xc0, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x01, 0xff, 0xff,
    0xff, 0xff, 0x80, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x01, 0xff, 0xff,
    0x7f, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfe, 0x7f, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xfe,
    0x3f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xfc, 0x1f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf8,
    0x0f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xf0, 0x03, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xc0,
};

// Cycle palette matching the GC screensaver's cycle_yuv[] order:
// red, green, yellow, blue, magenta, cyan, white.
static const uint16_t cycle_rgb[] = {
    RGB5(31,  0,  0),  // red
    RGB5( 0, 31,  0),  // green
    RGB5(31, 31,  0),  // yellow
    RGB5( 0,  0, 31),  // blue
    RGB5(31,  0, 31),  // magenta
    RGB5( 0, 31, 31),  // cyan
    RGB5(31, 31, 31),  // white
};
#define CYCLE_LEN ((int)(sizeof(cycle_rgb) / sizeof(cycle_rgb[0])))

// Mode 4 (8bpp indexed) with two pages at 0x06000000 / 0x0600A000.
// DISPCNT bit 4 selects which page is visible. Render to the off-screen
// page, flip during VBlank → no tearing. Palette: index 0 = black,
// index 1 = current cycle colour (changed instantly on each wall hit).
#define VRAM_PAGE0 ((volatile uint8_t *)0x06000000)
#define VRAM_PAGE1 ((volatile uint8_t *)0x0600A000)
#define BG_PAL     ((volatile uint16_t *)0x05000000)
#define PAGE_BYTES (GBA_W * GBA_H)

static volatile uint8_t *back_page;

static inline void fb_clear_page(volatile uint8_t *page)
{
    // Aligned 32-bit memset — page is 240*160 = 38400 bytes, multiple of 4.
    volatile uint32_t *p = (volatile uint32_t *)page;
    for (int i = 0; i < PAGE_BYTES / 4; i++) p[i] = 0;
}

static inline void fb_draw_logo(volatile uint8_t *page, int x, int y)
{
    for (int ly = 0; ly < LOGO_H; ly++) {
        int py = y + ly;
        if (py < 0 || py >= GBA_H) continue;
        const uint8_t *row = logo_mask + ly * LOGO_BPR;
        volatile uint8_t *fb_row = page + py * GBA_W;
        for (int lx = 0; lx < LOGO_W; lx++) {
            if (row[lx >> 3] & (0x80 >> (lx & 7))) {
                int px = x + lx;
                if (px >= 0 && px < GBA_W) fb_row[px] = 1;
            }
        }
    }
}

// Flip pages during VBlank. After flip, back_page points at the newly-
// hidden buffer (which now contains the previous frame's render — we
// clear + redraw it next frame).
static inline void fb_flip(void)
{
    while (REG_VCOUNT < 160) poll_reset();   // wait for vblank entry
    REG_DISPCNT ^= 0x0010;                    // toggle BG2 page select
    back_page = (REG_DISPCNT & 0x0010) ? VRAM_PAGE0 : VRAM_PAGE1;
}

// ============================================================================
// Live UI helpers
// ============================================================================

static void redraw_static_ui(bool joybus)
{
    consoleDemoInit();
    iprintf("\x1b[2;3HJOYPAD TESTER");
    iprintf("\x1b[3;3H=============");
    iprintf("\x1b[5;3HGC Link: %-10s", joybus ? "connected" : "standalone");
    iprintf("\x1b[7;3HButtons:");
    for (int i = 0; i < 10; i++) {
        int col = btn_pos[i].right ? COL_R : COL_L;
        iprintf("\x1b[%d;%dH%-6s[ ]", ROW0 + btn_pos[i].row, col, btn_name[i]);
    }
}

#define IDLE_THRESHOLD (60 * 30)  // 30 s at 60 Hz — matches GC IDLE_THRESHOLD_MS

int main(void)
{
    irqInit();
    irqEnable(IRQ_VBLANK);

    consoleDemoInit();

    iprintf("\x1b[2J");
    iprintf("\x1b[2;3HJOYPAD TESTER");
    iprintf("\x1b[3;3H=============");
    iprintf("\x1b[5;3HBooting...");

    // Doridian's joybus handshake — works under multiboot from a GC/Wii
    // host; on a flash cart there's no host to satisfy these bits, so
    // we time-out and proceed in standalone mode.
    REG_JOYTR = 0x30303030;
    int spin = 60 * 3;  // ~3 s at 60 Hz
    bool joybus = false;
    while (spin-- > 0 && (REG_JSTAT & 0b00001000)) wait_vblank();
    if (!(REG_JSTAT & 0b00001000)) {
        spin = 60 * 3;
        while (spin-- > 0 && !(REG_JSTAT & 0b00000010)) wait_vblank();
        joybus = (REG_JSTAT & 0b00000010) && (REG_JOYRE == 0x30303030);
    }

    if (joybus) irqSet(IRQ_VBLANK, on_vblank);

    redraw_static_ui(joybus);

    // Screensaver state. Start mid-screen drifting down-right, matching
    // the GC version's initial direction.
    uint32_t idle_frames = 0;
    bool     ss_on    = false;
    int      ss_x     = 80, ss_y = 50;
    int      ss_dx    = 1, ss_dy = 1;
    int      ss_color = 2;  // start at yellow, like GC
    uint32_t ss_frame = 0;

    while (1) {
        wait_vblank();
        REG_JOYTR = REG_KEYINPUT;

        uint16_t pressed = (~REG_KEYINPUT) & 0x03FF;

        if (pressed) {
            if (ss_on) {
                // Exit screensaver — re-init console and redraw live UI.
                redraw_static_ui(joybus);
                ss_on = false;
            }
            idle_frames = 0;
        } else if (!ss_on) {
            idle_frames++;
            if (idle_frames >= IDLE_THRESHOLD) {
                // Enter screensaver: switch to Mode 4 + BG2 (page 0
                // visible), zero both pages, install the current
                // cycle colour at palette index 1.
                BG_PAL[0] = 0;
                BG_PAL[1] = cycle_rgb[ss_color];
                REG_DISPCNT = 0x0004 | 0x0400;
                fb_clear_page(VRAM_PAGE0);
                fb_clear_page(VRAM_PAGE1);
                back_page = VRAM_PAGE1;
                ss_on = true;
                ss_frame = 0;
            }
        }

        if (ss_on) {
            // 60 Hz updates with half the per-step distance — same
            // pixel-per-second feel as the GC's 30 Hz × 4/3 step, but
            // visibly smoother since the GBA's progressive LCD doesn't
            // need the interlaced-field gating the GC version uses.
            ss_frame++;
            ss_x += ss_dx * 2;
            ss_y += ss_dy * 1;
            const int max_x = GBA_W - LOGO_W;
            const int max_y = GBA_H - LOGO_H;
            bool bounced = false;
            if (ss_x <= 0)     { ss_x = 0;     ss_dx = -ss_dx; bounced = true; }
            if (ss_x >= max_x) { ss_x = max_x; ss_dx = -ss_dx; bounced = true; }
            if (ss_y <= 0)     { ss_y = 0;     ss_dy = -ss_dy; bounced = true; }
            if (ss_y >= max_y) { ss_y = max_y; ss_dy = -ss_dy; bounced = true; }
            if (bounced) {
                ss_color = (ss_color + 1) % CYCLE_LEN;
                BG_PAL[1] = cycle_rgb[ss_color];
            }

            fb_clear_page(back_page);
            fb_draw_logo(back_page, ss_x, ss_y);
            fb_flip();
            continue;
        }

        // Live UI — only update the indicator chars, not the labels.
        for (int i = 0; i < 10; i++) {
            int col = btn_pos[i].right ? IND_R : IND_L;
            iprintf("\x1b[%d;%dH%c", ROW0 + btn_pos[i].row, col,
                    (pressed & (1 << i)) ? 'X' : ' ');
        }
        iprintf("\x1b[14;3HRaw: %04X", (unsigned)pressed);
    }

    return 0;
}
