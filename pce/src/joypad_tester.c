/*
 * Joypad Tester - PC Engine
 *
 * Extends dshadoff's PCE_Mouse_Test baseline with a title bar and an
 * idle screensaver that bounces the joypad logo across the screen,
 * color-cycling on every wall bounce. Matches the artwork, palette,
 * and motion of the GameCube and GBA testers in this repo.
 *
 * NOTE: Keep this file pure ASCII. The HuC tokenizer (uli/huc) silently
 * mis-parses functions whose comments contain multi-byte UTF-8 chars
 * (em-dashes, curly quotes, etc.), dropping bodies until it recovers.
 *
 * Baseline:
 *   https://github.com/dshadoff/PCE_Mouse_Test
 *
 * Copyright (c) 2022 David Shadoff
 * Copyright (c) 2026 Robert Dale Smith
 *
 * Licensed under MIT - see ../LICENSE.md.
 */

#include "huc.h"
#include "gen_logo.h"

/* PCE VCE color format: 9-bit GGG RRR BBB */
#define COLOR_BLACK    0x000
#define COLOR_RED      0x038
#define COLOR_GREEN    0x1C0
#define COLOR_YELLOW   0x1F8
#define COLOR_BLUE     0x007
#define COLOR_MAGENTA  0x03F
#define COLOR_CYAN     0x1C7
#define COLOR_WHITE    0x1FF

#define SCREEN_W           256
#define SCREEN_H           224
#define IDLE_THRESHOLD     1800   /* 30s at 60fps */
#define NUM_CYCLE_COLORS   7

/* VRAM layout: 4x 32x32 sprite quadrants of the logo, 256 words each.
 * Park them past the BG tile / font area at $5000. */
#define VRAM_LOGO_Q0  0x5000
#define VRAM_LOGO_Q1  0x5100
#define VRAM_LOGO_Q2  0x5200
#define VRAM_LOGO_Q3  0x5300

/* Sprite palette 0 color 1: set_color() index 257 = sprite palette 0
 * color 1 (color 0 is always transparent). The screensaver rewrites
 * this entry on each wall bounce to cycle the logo color. */
#define SPR_COLOR_INDEX  257

static const int cycle_colors[NUM_CYCLE_COLORS] = {
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE
};

static int abs_x, abs_y;
static int idle_frames;
static int prev_joy0;

static int ss_x, ss_y;
static int ss_dx, ss_dy;
static int ss_color_idx;
static int ss_skip;

apply_palettes()
{
    /* BG palette 0: white body text on black */
    set_color(0,  COLOR_BLACK);
    set_color(1,  COLOR_WHITE);
    /* BG palette 1: yellow title */
    set_color(16, COLOR_BLACK);
    set_color(17, COLOR_YELLOW);
    /* BG palette 2: cyan instructions */
    set_color(32, COLOR_BLACK);
    set_color(33, COLOR_CYAN);

    /* Sprite palette 0 color 1: logo foreground (cycles in screensaver). */
    set_color(SPR_COLOR_INDEX, COLOR_RED);
}

load_logo_sprites()
{
    /* Each 32x32 PCE sprite = 256 words. load_vram takes
     * (vram_word_addr, data_ptr, size_in_words). */
    load_vram(VRAM_LOGO_Q0, logo_q0, 256);
    load_vram(VRAM_LOGO_Q1, logo_q1, 256);
    load_vram(VRAM_LOGO_Q2, logo_q2, 256);
    load_vram(VRAM_LOGO_Q3, logo_q3, 256);
}

setup_logo_sprites()
{
    /* Hardware sprites 0-3 form a 2x2 grid of 32x32 cells covering the
     * 64x64 logo. Pattern, size, palette, and priority are set once;
     * x/y move every frame during the screensaver. */
    spr_set(0);
    spr_pattern(VRAM_LOGO_Q0);
    spr_ctrl(SIZE_MAS, SZ_32x32);
    spr_pal(0);
    spr_pri(1);
    spr_hide();

    spr_set(1);
    spr_pattern(VRAM_LOGO_Q1);
    spr_ctrl(SIZE_MAS, SZ_32x32);
    spr_pal(0);
    spr_pri(1);
    spr_hide();

    spr_set(2);
    spr_pattern(VRAM_LOGO_Q2);
    spr_ctrl(SIZE_MAS, SZ_32x32);
    spr_pal(0);
    spr_pri(1);
    spr_hide();

    spr_set(3);
    spr_pattern(VRAM_LOGO_Q3);
    spr_ctrl(SIZE_MAS, SZ_32x32);
    spr_pal(0);
    spr_pri(1);
    spr_hide();

    satb_update();
}

position_logo(int x, int y)
{
    spr_set(0); spr_x(x);       spr_y(y);
    spr_set(1); spr_x(x + 32);  spr_y(y);
    spr_set(2); spr_x(x);       spr_y(y + 32);
    spr_set(3); spr_x(x + 32);  spr_y(y + 32);
    satb_update();
}

show_logo()
{
    spr_set(0); spr_show();
    spr_set(1); spr_show();
    spr_set(2); spr_show();
    spr_set(3); spr_show();
    satb_update();
}

hide_logo()
{
    spr_set(0); spr_hide();
    spr_set(1); spr_hide();
    spr_set(2); spr_hide();
    spr_set(3); spr_hide();
    satb_update();
}

draw_main_screen()
{
    cls();
    apply_palettes();

    set_font_pal(1);
    put_string("Joypad Tester - PC Engine", 3, 1);
    put_string("-------------------------", 3, 2);

    set_font_pal(0);
    put_string("P1:", 4, 5);
    put_string("P2:", 4, 6);
    put_string("P3:", 4, 7);
    put_string("P4:", 4, 8);
    put_string("P5:", 4, 9);

    put_string("Mouse:", 18, 5);
    put_string("  x:", 18, 6);
    put_string("  y:", 18, 7);

    put_string("abs x:", 4, 12);
    put_string("abs y:", 4, 13);

    set_font_pal(2);
    put_string("Press I button or right-click", 1, 17);
    put_string("to toggle mouse mode.", 1, 18);

    set_font_pal(0);
}

int input_active()
{
    /* Edge-detected: any change in held buttons, or any mouse motion.
     * Does NOT match against joy(0) being non-zero, so unconnected
     * multitap slots reporting a static non-zero idle pattern don't
     * defeat the screensaver. */
    return (joy(0) != prev_joy0) || mouse_x() || mouse_y();
}

bounce_cycle()
{
    ss_color_idx = (ss_color_idx + 1) % NUM_CYCLE_COLORS;
    set_color(SPR_COLOR_INDEX, cycle_colors[ss_color_idx]);
}

run_screensaver()
{
    cls();

    /* Step rate (dx=2, dy=1) mirrors the GBA tester so motion reads the
     * same on both. */
    ss_x = 16;
    ss_y = 16;
    ss_dx = 2;
    ss_dy = 1;
    ss_color_idx = 0;
    ss_skip = 0;
    set_color(SPR_COLOR_INDEX, cycle_colors[ss_color_idx]);

    show_logo();
    position_logo(ss_x, ss_y);

    while (!input_active()) {
        vsync(0);

        /* Skip motion every 4th frame to slow the bounce by ~25%. */
        ss_skip++;
        if (ss_skip >= 4) {
            ss_skip = 0;
            continue;
        }

        ss_x += ss_dx;
        ss_y += ss_dy;

        if (ss_x <= 0) {
            ss_x = 0;
            ss_dx = -ss_dx;
            bounce_cycle();
        } else if (ss_x >= SCREEN_W - LOGO_W) {
            ss_x = SCREEN_W - LOGO_W;
            ss_dx = -ss_dx;
            bounce_cycle();
        }
        if (ss_y <= 0) {
            ss_y = 0;
            ss_dy = -ss_dy;
            bounce_cycle();
        } else if (ss_y >= SCREEN_H - LOGO_H) {
            ss_y = SCREEN_H - LOGO_H;
            ss_dy = -ss_dy;
            bounce_cycle();
        }

        position_logo(ss_x, ss_y);
    }

    hide_logo();

    /* Consume the wake-button edge so it doesn't immediately toggle
     * mouse mode in the main loop. */
    vsync(0);
    prev_joy0 = joy(0);
}

main()
{
    abs_x = 0;
    abs_y = 0;
    idle_frames = 0;
    prev_joy0 = 0;

    init_satb();
    apply_palettes();
    load_logo_sprites();
    setup_logo_sprites();
    draw_main_screen();

    for (;;) {
        set_font_pal(0);
        put_hex(joy(0), 4, 8, 5);
        put_hex(joy(1), 4, 8, 6);
        put_hex(joy(2), 4, 8, 7);
        put_hex(joy(3), 4, 8, 8);
        put_hex(joy(4), 4, 8, 9);

        put_hex(mouse_exists(), 2, 26, 5);
        put_hex(mouse_x(),      2, 26, 6);
        put_hex(mouse_y(),      2, 26, 7);

        abs_x -= mouse_x();
        abs_y -= mouse_y();
        put_hex(abs_x, 4, 12, 12);
        put_hex(abs_y, 4, 12, 13);

        if (joytrg(0) & JOY_A) {
            if (mouse_exists()) {
                mouse_disable();
            } else {
                mouse_enable();
            }
        }

        if (input_active()) {
            idle_frames = 0;
        } else {
            idle_frames++;
            if (idle_frames >= IDLE_THRESHOLD) {
                run_screensaver();
                idle_frames = 0;
                draw_main_screen();
            }
        }
        prev_joy0 = joy(0);

        vsync(0);
    }

    return;
}
