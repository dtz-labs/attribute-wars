/*
 * title.h -- the title screen: game name, control-scheme menu, sound menu.
 *
 * Only screen A is shown here (no page-flip), so one static text draw plus a
 * few per-frame attribute-row highlights is the whole cost. That headroom pays
 * for the shine sweep -- a glint walking across each text row -- which is
 * therefore Timex/48K only; the ZX128 build skips it.
 */
#ifndef TITLE_H
#define TITLE_H

#include "types.h"

/* Draw the menu and block until the player presses 0 (START).
 *
 *   1/2/3 pick the control scheme  -> *ctrl_out  (CTRL_* from controls.h)
 *   4/5/6 pick the sound mode      -> *sound_out (SOUND_* from music.h)
 *
 * `initial_sound` seeds the sound selection, so returning from a game keeps a
 * running tune alive until the player explicitly picks BEEPER or FX.
 */
void title_screen(u8 *ctrl_out, u8 *sound_out, u8 initial_sound);

#endif /* TITLE_H */
