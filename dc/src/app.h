/*
 * app.h — top-level shared types for Joypad Tester Dreamcast.
 *
 * Two things every translation unit in this app cares about:
 *   1. The per-port state model (ports/ports.h) — what's plugged in,
 *      what the buttons/axes read, what slot peripherals respond.
 *   2. The mode registry — which view is currently rendering, and
 *      how the options menu drives transitions between them.
 *
 * Keep this header lean. Per-subsystem details live in their own
 * headers (input/cursor.h, video/mode.h, etc.).
 */
#ifndef JT_APP_H
#define JT_APP_H

#include <stdint.h>
#include <stddef.h>

/* The version string is injected by the Makefile (-D flag) from the
 * VERSION file at the dc/ root. The fallback below only kicks in for
 * out-of-tree builds where the Makefile isn't doing that work. */
#ifndef JT_VERSION_STR
#define JT_VERSION_STR "0.0.0-dev"
#endif

/* Modes the options menu can switch between. New modes append to
 * this enum AND register a jt_mode_t entry in main.c's mode_table[].
 * Order here determines menu order. */
typedef enum {
    JT_MODE_TESTER = 0,
    JT_MODE_BROWSER,        /* VMU File Manager */
    JT_MODE_VMU_EDITOR,
    JT_MODE_LIB_BROWSER,    /* VMU Icon Library */
    JT_MODE_ABOUT,
    JT_MODE_COUNT
} jt_mode_id_t;

/* Mode interface. Each modes/<name>.c implements update() + draw()
 * and exports a jt_mode_t with the function pointers. Modes share
 * the ports[] state and the cursor; everything else is mode-local. */
typedef struct jt_mode {
    const char *name;          /* shown in the options menu */
    void (*enter)(void);       /* called once when switched to */
    void (*leave)(void);       /* called once when switched away */
    void (*update)(float dt);  /* per-frame logic; dt in seconds */
    void (*draw)(void);        /* per-frame render */
} jt_mode_t;

/* Globals owned by main.c, declared here so modes can read them. */
extern jt_mode_id_t jt_current_mode;
void jt_request_mode(jt_mode_id_t next);

#endif /* JT_APP_H */
