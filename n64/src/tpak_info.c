/*
 * tpak_info.c — Transfer Pak cart-header reader.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */

#include "tpak_info.h"

#include <string.h>
#include <libdragon.h>

static tpak_info_t cache[JOYBUS_PORT_COUNT];

const tpak_info_t *tpak_info_read(joypad_port_t port)
{
    if (cache[port].attempted) {
        return &cache[port];
    }

    cache[port].attempted = true;
    cache[port].valid     = false;
    cache[port].title[0]  = '\0';

    /* tpak_init powers the pak, sets it to "access mode 1" so the
     * GB cart's memory map is visible through the Transfer Pak
     * window, and verifies the magic bytes. Returns 0 on success. */
    if (tpak_init(port) != 0) {
        return &cache[port];
    }

    struct gameboy_cartridge_header hdr = {0};
    if (tpak_get_cartridge_header(port, &hdr) != 0) {
        return &cache[port];
    }
    if (!tpak_check_header(&hdr)) {
        /* Title may still be readable even if the header checksum is
         * wrong (e.g. a homebrew cart that didn't bother computing
         * it); copy it anyway and just flag as not-valid. */
        memcpy(cache[port].title, hdr.title, 16);
        cache[port].title[16] = '\0';
        return &cache[port];
    }

    cache[port].valid     = true;
    memcpy(cache[port].title, hdr.title, 16);
    cache[port].title[16] = '\0';
    /* Strip control characters from the title so console_render
     * doesn't choke on stray bytes from unusual carts. */
    for (int i = 0; i < 16; i++) {
        if (cache[port].title[i] < 0x20 || cache[port].title[i] > 0x7e) {
            cache[port].title[i] = '\0';
            break;
        }
    }
    cache[port].cart_type = hdr.cartridge_type;
    cache[port].rom_size  = hdr.rom_size_code;
    cache[port].ram_size  = hdr.ram_size_code;

    return &cache[port];
}

void tpak_info_reset(joypad_port_t port)
{
    memset(&cache[port], 0, sizeof(cache[port]));
}
