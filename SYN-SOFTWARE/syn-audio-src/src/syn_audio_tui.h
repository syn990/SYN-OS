/* ------------------------------------------------------------------------
 *   ncurses UI for syn-audio: two scrollable lists (outputs/inputs) with
 *   volume bars, mute state, and a default-device marker. Same widget
 *   shape as syn-wifi's network list. Colored from the live SYN-OS theme.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUDIO (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_AUDIO_TUI_H
#define SYN_AUDIO_TUI_H

#include "syn_pulse.h"
#include <stddef.h>

void syn_audio_tui_init(void);
void syn_audio_tui_end(void);

/* Which of the two device lists has focus. */
typedef enum {
	SYN_AUDIO_TAB_OUTPUTS = 0,
	SYN_AUDIO_TAB_INPUTS = 1,
} syn_audio_tab;

/* Actions the dashboard screen can return for the currently-selected
 * device in the currently-focused tab. */
typedef enum {
	SYN_AUDIO_ACTION_NONE = 0,   /* arrow-key move only — caller just redraws with new index */
	SYN_AUDIO_ACTION_SWITCH_TAB, /* Tab pressed — caller flips focused_tab and redraws */
	SYN_AUDIO_ACTION_QUIT,       /* Esc/q */
	SYN_AUDIO_ACTION_SET_DEFAULT,
	SYN_AUDIO_ACTION_TOGGLE_MUTE,
	SYN_AUDIO_ACTION_VOLUME_UP,   /* +5 */
	SYN_AUDIO_ACTION_VOLUME_DOWN, /* -5 */
} syn_audio_action;

typedef struct {
	syn_audio_action action;
	syn_audio_tab tab;   /* which list the action applies to (unchanged by this call except NONE's index moves) */
	int index;           /* selected device's index within that list, -1 if list empty */
} syn_audio_input_result;

/* Renders both device lists (outputs and inputs) with the given tab
 * focused/selected row highlighted; blocks for one keypress and returns
 * what the caller should do. The caller owns the actual pulse operation
 * and re-fetches fresh device lists before the next call — this function
 * never touches syn_pulse itself. */
syn_audio_input_result syn_audio_tui_dashboard(
	const syn_pulse_device *outputs, int output_count,
	const syn_pulse_device *inputs, int input_count,
	syn_audio_tab focused_tab, int selected_index);

/* Centered message with "press any key to continue" — used for a fatal
 * startup error (no pulse connection) before falling back to a plain
 * terminal. */
void syn_audio_tui_message(const char *title, const char *body);

#endif
