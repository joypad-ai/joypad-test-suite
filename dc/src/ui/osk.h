/*
 * osk.h — on-screen keyboard widget for text entry without a keyboard.
 *
 * Single global text-input session. Whoever opens the OSK provides a
 * label + the initial text + a max length; the session collects
 * characters from any source (cursor click on a key, D-pad navigate
 * + A on a key, or typed scancodes from a real maple keyboard if
 * one is plugged in) and reports completion via jt_osk_consume_*.
 *
 * UX model:
 *   - Modal overlay; while visible, the active mode does not update.
 *   - Layout: 6 rows x 10 cols of keys, with shift / layer / space /
 *     backspace / done across the bottom rows.
 *   - Three layers: lowercase, UPPERCASE, symbols + numbers.
 *   - Input sources all converge on jt_osk_input(int ascii):
 *       OSK key press -> the ASCII of the key
 *       Maple kbd     -> the ASCII KOS hands back
 *
 * Restriction: ASCII only in v0.2.x. Description fields on the VMU
 * are Shift-JIS in the official spec; ASCII is JIS-safe in the low
 * range so this is OK to write, but a JIS character picker is a v0.3+
 * deliverable when localized naming becomes a real ask.
 */
#ifndef JT_OSK_H
#define JT_OSK_H

#include <stdbool.h>
#include <stddef.h>

#define JT_OSK_MAX_LEN 32

/* Begin a text-entry session. `label` shows above the input box.
 * `initial` is copied into the buffer as the starting text. `max_len`
 * caps the buffer (clamped to JT_OSK_MAX_LEN). After this returns,
 * jt_osk_visible() will be true and the OSK paints itself each frame
 * via jt_osk_draw(); the host should skip its own draw to avoid the
 * single-buffer overlap flicker pattern we already squash in main.c. */
void jt_osk_begin(const char *label, const char *initial, size_t max_len);

void jt_osk_update(float dt);
void jt_osk_draw(void);
bool jt_osk_visible(void);

/* After the user presses "Done" (or Enter on a maple keyboard),
 * jt_osk_visible flips to false and jt_osk_consume_text returns
 * true once with the accepted text. Subsequent calls return false
 * until the next session. Cancel (Esc / B in OSK) discards the text
 * and consume returns false. */
bool jt_osk_consume_text(char *out, size_t cap);

/* Feed a raw ASCII character into the active session. Special
 * codes: \b (0x08) = backspace, \r (0x0D) = done, 0x1B = cancel. */
void jt_osk_input(int ascii);

#endif /* JT_OSK_H */
