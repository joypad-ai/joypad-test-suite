/*
 * lib_browser.h — Library browser mode (reads VMUICONS.VMS entries).
 *
 * Separate from the save browser: it lists library entries (icons we
 * saved earlier via Save-to-library, plus auto-backups stashed by
 * Apply with backup-on-replace) across all detected VMUs. Picks one,
 * loads it into the editor.
 */
#ifndef JT_MODE_LIB_BROWSER_H
#define JT_MODE_LIB_BROWSER_H

#include "../app.h"

extern const jt_mode_t jt_mode_lib_browser;

#endif
