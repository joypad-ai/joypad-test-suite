/*
 * tpak_info.h — Transfer Pak / Game Boy cartridge header reader.
 *
 * When a Transfer Pak is detected on a controller port, libdragon's
 * tpak_* API can wake it, power the slotted GB cart, and read the
 * 0x100..0x14F cartridge header. We do that once per port detection
 * and cache the title + size codes so subsequent frames don't re-do
 * the SI traffic.
 *
 * Copyright (c) 2026 Robert Dale Smith
 * MIT License -- see ../LICENSE.md
 */
#ifndef N64_TPAK_INFO_H
#define N64_TPAK_INFO_H

#include <libdragon.h>
#include <stdbool.h>

typedef struct {
    bool    valid;           /* header passed tpak_check_header */
    bool    attempted;       /* avoid re-reading every frame */
    char    title[17];       /* GB cartridge title (null-terminated) */
    uint8_t cart_type;       /* raw byte from header 0x0147 */
    uint8_t rom_size;        /* raw byte from header 0x0148 */
    uint8_t ram_size;        /* raw byte from header 0x0149 */
} tpak_info_t;

/* Read the cartridge header on the given port if not already
 * attempted. Returns the cached snapshot afterwards. */
const tpak_info_t *tpak_info_read(joypad_port_t port);

/* Reset the per-port cache (e.g. when the accessory type stops
 * being Transfer Pak so a future re-insert re-reads). */
void tpak_info_reset(joypad_port_t port);

#endif
