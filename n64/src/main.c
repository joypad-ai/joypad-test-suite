/*
 * Joypad Tester - Nintendo 64
 *
 * Forked from Christopher Bonhage (meeq)'s JoypadTest-N64 example
 * (public domain), retargeted to LibDragon trunk's joypad subsystem
 * and laid out to match the GameCube tester (per-port row block:
 * Port N Style/Pak/Rumble line, then Stick + button rows). Adds:
 *  - GBA-on-Joybus identification + Kawasedo multiboot upload from
 *    the embedded gba_payload[] (see src/gba.c).
 *  - N64 Mouse delta accumulation into an absolute position.
 *  - Transfer Pak Game Boy cartridge header peek (title + sizes).
 *  - Idle screensaver: 76x64 bouncing joypad logo (see screensaver.c).
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include <string.h>
#include <libdragon.h>

#include "gba.h"
#include "mouse.h"
#include "screensaver.h"
#include "tpak_info.h"

/* Track per-port GBA-boot status so we can display "Booting...",
 * "Booted OK", or "Boot failed (code -N)" inline with the port row. */
typedef enum {
    GBA_STATE_IDLE = 0,
    GBA_STATE_BOOTING,
    GBA_STATE_BOOTED,
    GBA_STATE_FAILED,
} gba_state_t;

static gba_state_t gba_state[JOYBUS_PORT_COUNT];
static int         gba_last_err[JOYBUS_PORT_COUNT];
static uint8_t     gba_last_input[JOYBUS_PORT_COUNT][2];

/* Cached previous accessory so we can flush per-port helpers when an
 * accessory is removed. */
static joypad_accessory_type_t prev_acc[JOYBUS_PORT_COUNT];

/* Match the column widths of the GameCube tester's printf templates
 * so the layout reads identically across the two ROMs. */
static const char *style_label(joypad_style_t s, joybus_identifier_t id)
{
    /* GBA-on-Joybus doesn't have a joypad_style_t -- it falls through
     * to JOYPAD_STYLE_NONE -- so we override the label off the raw
     * identifier when we see one. */
    if (id == JOYBUS_IDENTIFIER_GBA_LINK_CABLE) return "GBA    ";
    switch (s) {
        case JOYPAD_STYLE_N64:   return "N64    ";
        case JOYPAD_STYLE_GCN:   return "GCN    ";
        case JOYPAD_STYLE_MOUSE: return "Mouse  ";
        case JOYPAD_STYLE_NONE:
        default:                 return "None   ";
    }
}

static const char *pak_label(joypad_accessory_type_t a)
{
    switch (a) {
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "Memory      ";
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:     return "Rumble Pak  ";
        case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK:   return "Transfer Pak";
        case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR:     return "Bio Sensor  ";
        case JOYPAD_ACCESSORY_TYPE_SNAP_STATION:   return "Snap Station";
        case JOYPAD_ACCESSORY_TYPE_NONE:
        default:                                   return "None        ";
    }
}

static const char *rumble_label(bool supported, bool active)
{
    if (!supported) return "Unavailable";
    if (active)     return "Active     ";
    return "Idle       ";
}

/* True if the port's state shows any non-trivial activity. Used to
 * decide whether to reset the idle-screensaver counter. */
static bool port_has_input(joypad_port_t port)
{
    joypad_buttons_t held = joypad_get_buttons_held(port);
    if (held.a || held.b || held.x || held.y || held.l || held.r ||
        held.z || held.start ||
        held.d_up || held.d_down || held.d_left || held.d_right ||
        held.c_up || held.c_down || held.c_left || held.c_right) {
        return true;
    }
    joypad_inputs_t in = joypad_get_inputs(port);
    const int DEAD = 8;
    if (in.stick_x  > DEAD || in.stick_x  < -DEAD) return true;
    if (in.stick_y  > DEAD || in.stick_y  < -DEAD) return true;
    if (in.cstick_x > DEAD || in.cstick_x < -DEAD) return true;
    if (in.cstick_y > DEAD || in.cstick_y < -DEAD) return true;
    if (in.analog_l > DEAD || in.analog_r > DEAD)  return true;
    return false;
}

static void print_port(joypad_port_t port)
{
    joybus_identifier_t     id    = joypad_get_identifier(port);
    joypad_style_t          style = joypad_get_style(port);
    joypad_accessory_type_t acc   = joypad_get_accessory_type(port);
    joypad_inputs_t         in    = joypad_get_inputs(port);
    bool rumble_sup = joypad_get_rumble_supported(port);
    bool rumble_act = joypad_get_rumble_active(port);

    /* Line 1: Port N / Style / Pak / Rumble (or Boot for GBA). The
     * GameCube tester's right-side payload swaps Rumble for Boot
     * status when a GBA is bound; we do the same. */
    printf("Port %d Style: %s ", port + 1, style_label(style, id));

    if (id == JOYBUS_IDENTIFIER_GBA_LINK_CABLE) {
        const char *boot;
        switch (gba_state[port]) {
            case GBA_STATE_BOOTED:  boot = "Booted   ";       break;
            case GBA_STATE_BOOTING: boot = "Booting..."; break;
            case GBA_STATE_FAILED:  boot = "BootFail "; break;
            case GBA_STATE_IDLE:
            default:                boot = "BootIdle "; break;
        }
        if (gba_state[port] == GBA_STATE_FAILED) {
            printf("Boot: %s err%+d         \n", boot, gba_last_err[port]);
        } else if (gba_state[port] == GBA_STATE_BOOTED) {
            printf("Boot: %s poll %02x %02x      \n",
                boot, gba_last_input[port][0], gba_last_input[port][1]);
        } else {
            printf("Boot: %s (press A to multiboot)\n", boot);
        }
    } else {
        printf("Pak: %s Rumble: %s\n",
            pak_label(acc), rumble_label(rumble_sup, rumble_act));
    }

    /* No more lines for disconnected ports -- just a blank break. */
    if (style == JOYPAD_STYLE_NONE
        && id != JOYBUS_IDENTIFIER_GBA_LINK_CABLE) {
        printf("\n");
        return;
    }

    /* Mouse: render absolute position alongside the per-frame delta. */
    if (style == JOYPAD_STYLE_MOUSE) {
        int ax, ay;
        mouse_get(port, &ax, &ay);
        printf("Abs: %+05d,%+05d   Delta: %+03d,%+03d\n",
               ax, ay, in.stick_x, in.stick_y);
        /* Mouse only has L+R buttons. libdragon maps them onto btn.a / btn.b. */
        printf("L:%d R:%d\n\n", in.btn.a, in.btn.b);
        return;
    }

    /* Identical column layout to gcn/ppc/main.c::print_port(). */
    printf("Stick: %+04d,%+04d C-Stick: %+04d,%+04d L-Trig:%03u R-Trig:%03u\n",
        in.stick_x,  in.stick_y,
        in.cstick_x, in.cstick_y,
        in.analog_l, in.analog_r);
    printf("A:%d B:%d X:%d Y:%d L:%d R:%d Z:%d Start:%d\n",
        in.btn.a, in.btn.b, in.btn.x, in.btn.y,
        in.btn.l, in.btn.r, in.btn.z, in.btn.start);
    printf("D-U:%d D-D:%d D-L:%d D-R:%d C-U:%d C-D:%d C-L:%d C-R:%d\n",
        in.btn.d_up, in.btn.d_down, in.btn.d_left, in.btn.d_right,
        in.btn.c_up, in.btn.c_down, in.btn.c_left, in.btn.c_right);

    /* Transfer Pak: one-shot cart-header read, title + size codes
     * underneath the standard port block. */
    if (acc == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK) {
        const tpak_info_t *t = tpak_info_read(port);
        if (t->title[0]) {
            printf("  GB cart: \"%s\" type:%02x rom:%02x ram:%02x%s\n",
                t->title, t->cart_type, t->rom_size, t->ram_size,
                t->valid ? "" : " (chksum bad)");
        } else if (t->attempted) {
            printf("  GB cart: no cart / header unreadable\n");
        }
    }
    printf("\n");
}

int main(void)
{
    timer_init();
    /* 320x240 is N64's standard non-interlaced framebuffer. Each fb
     * pixel = 2 TV dots horizontally (anamorphic), so square-pixel
     * PNGs render as horizontally squished. We compensate by using
     * a pre-widened logo source (see assets/logo_64.png at 76x64,
     * vendored from 3do/assets/ which solves the same problem). */
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE,
                 FILTERS_RESAMPLE);
    joypad_init();

    console_init();
    console_set_render_mode(RENDER_MANUAL);
    console_set_debug(false);

    while (1) {
        joypad_poll();

        /* Per-port maintenance: mouse delta accumulation + Transfer
         * Pak cache invalidation when accessories change. */
        bool any_input = false;
        JOYPAD_PORT_FOREACH (port) {
            joypad_style_t          style = joypad_get_style(port);
            joypad_accessory_type_t acc   = joypad_get_accessory_type(port);

            if (style == JOYPAD_STYLE_MOUSE) mouse_tick(port);

            if (prev_acc[port] == JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK
                && acc != JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK) {
                tpak_info_reset(port);
            }
            prev_acc[port] = acc;

            if (port_has_input(port)) any_input = true;
        }

        screensaver_idle_tick(any_input);

        /* GBA multiboot trigger gated to NOT run while screensaver is
         * up so a stray wake-press doesn't also kick off an upload. */
        if (!screensaver_active()) {
            bool any_a_pressed = false;
            JOYPAD_PORT_FOREACH (port) {
                joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
                if (pressed.a) { any_a_pressed = true; break; }
            }
            if (any_a_pressed) {
                JOYPAD_PORT_FOREACH (port) {
                    if (gba_state[port] == GBA_STATE_IDLE
                        && joypad_get_identifier(port)
                           == JOYBUS_IDENTIFIER_GBA_LINK_CABLE) {
                        gba_state[port] = GBA_STATE_BOOTING;
                        int err = gba_boot_embedded(port);
                        if (err == 0) gba_state[port] = GBA_STATE_BOOTED;
                        else {
                            gba_state[port] = GBA_STATE_FAILED;
                            gba_last_err[port] = err;
                        }
                    }
                }
            }
            JOYPAD_PORT_FOREACH (port) {
                if (gba_state[port] == GBA_STATE_BOOTED) {
                    gba_poll_input(port, gba_last_input[port]);
                }
            }
        }

        if (screensaver_active()) {
            screensaver_render();
        } else {
            console_clear();

            printf("Joypad Tester - N64\n\n");

            JOYPAD_PORT_FOREACH (port) {
                print_port(port);
            }

            console_render();
        }
    }
}
