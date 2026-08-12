/* ------------------------------------------------------------------------
 *                          S Y N - A U D I O
 *
 *   Native audio control talking to pipewire-pulse/PulseAudio directly via
 *   libpulse (see syn_pulse.h) — no pactl subprocess, no pavucontrol.
 *
 *   Run with no arguments for the interactive ncurses dashboard (see
 *   syn_audio_tui.h), themed from the live SYN-OS theme (see syn_theme.h).
 *   Flag form below is for scripting/menu.xml — same shape as syn-crypter
 *   and syn-connect.
 *
 *   Usage:
 *     syn-audio                                              (interactive)
 *     syn-audio --list-sinks | --list-sources | --list-cards
 *     syn-audio --set-default-sink|--set-default-source <name>
 *     syn-audio --mute-sink|--mute-source <name> on|off|toggle
 *     syn-audio --set-sink-volume|--set-source-volume <name> <0-100>
 *     syn-audio --adjust-sink-volume|--adjust-source-volume <name> <+-N>
 *     syn-audio --set-profile <card-name> <profile-name>
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUDIO (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_pulse.h"
#include "syn_audio_tui.h"
#include "syn_bar_notify.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_DEVICES 64
#define MAX_CARDS 8
#define MAX_PROFILE_ROWS (MAX_CARDS * SYN_PULSE_MAX_PROFILES)

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s                                                        (interactive)\n"
		"       %s --list-sinks | --list-sources | --list-cards\n"
		"       %s --set-default-sink|--set-default-source <name>\n"
		"       %s --mute-sink|--mute-source <name> on|off|toggle\n"
		"       %s --set-sink-volume|--set-source-volume <name> <0-100>\n"
		"       %s --adjust-sink-volume|--adjust-source-volume <name> <+-N>\n"
		"       %s --set-profile <card-name> <profile-name>\n",
		argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

/* One line per profile: card name, profile name, description, active/-,
 * available/-. Tab-separated, same shape as print_device_list(). */
static void print_card_list(const syn_pulse_card *cards, int count) {
	for (int c = 0; c < count && c < MAX_CARDS; c++) {
		const syn_pulse_card *card = &cards[c];
		for (int i = 0; i < card->profile_count; i++) {
			const syn_pulse_profile *prof = &card->profiles[i];
			printf("%s\t%s\t%s\t%s\t%s\n",
				card->name, prof->name, prof->description,
				(card->active_profile == i) ? "active" : "-",
				prof->available ? "available" : "unavailable");
		}
	}
}

/* default and muted are independent — a device can be both at once, so
 * each gets its own field rather than one collapsed state string. */
static void print_device_list(const syn_pulse_device *devices, int count) {
	for (int i = 0; i < count && i < MAX_DEVICES; i++) {
		const syn_pulse_device *d = &devices[i];
		printf("%u\t%s\t%s\t%s\t%s\t%d%%\n",
			d->index, d->name, d->description,
			d->is_default ? "default" : "-",
			d->muted ? "muted" : "-",
			d->volume_pct);
	}
}

/* Looks up `name`'s current mute state from a fresh device list —
 * needed because syn_pulse_toggle_sink_mute()/etc. only report whether
 * the toggle operation itself succeeded, not which way it landed.
 * Returns false (treated as "assume unmuted") if the device can't be
 * found, which only happens if it was unplugged between the mute call
 * and this lookup — rare enough that guessing wrong here just means
 * one cue plays backwards, not a real failure. */
static bool device_is_muted(syn_pulse *p, bool is_sink, const char *name) {
	syn_pulse_device devices[MAX_DEVICES];
	char err[256];
	int count = is_sink
		? syn_pulse_list_sinks(p, devices, MAX_DEVICES, err, sizeof(err))
		: syn_pulse_list_sources(p, devices, MAX_DEVICES, err, sizeof(err));
	for (int i = 0; i < count; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			return devices[i].muted;
		}
	}
	return false;
}

static int run_cli(int argc, char **argv) {
	char err[256] = {0};
	syn_pulse *p = syn_pulse_open(err, sizeof(err));
	if (!p) {
		fprintf(stderr, "syn-audio: %s\n", err);
		return 1;
	}

	int rc = 0;
	const char *cmd = argv[1];

	if (strcmp(cmd, "--list-sinks") == 0) {
		syn_pulse_device devices[MAX_DEVICES];
		int count = syn_pulse_list_sinks(p, devices, MAX_DEVICES, err, sizeof(err));
		if (count < 0) { fprintf(stderr, "syn-audio: %s\n", err); rc = 1; }
		else print_device_list(devices, count);

	} else if (strcmp(cmd, "--list-sources") == 0) {
		syn_pulse_device devices[MAX_DEVICES];
		int count = syn_pulse_list_sources(p, devices, MAX_DEVICES, err, sizeof(err));
		if (count < 0) { fprintf(stderr, "syn-audio: %s\n", err); rc = 1; }
		else print_device_list(devices, count);

	} else if (strcmp(cmd, "--list-cards") == 0) {
		syn_pulse_card cards[MAX_CARDS];
		int count = syn_pulse_list_cards(p, cards, MAX_CARDS, err, sizeof(err));
		if (count < 0) { fprintf(stderr, "syn-audio: %s\n", err); rc = 1; }
		else print_card_list(cards, count);

	} else if (strcmp(cmd, "--set-profile") == 0) {
		if (argc != 4) { print_usage(argv[0]); rc = 1; goto done; }
		bool ok = syn_pulse_set_card_profile(p, argv[2], argv[3], err, sizeof(err));
		if (!ok) {
			fprintf(stderr, "syn-audio: %s\n", err); rc = 1;
			syn_bar_notify_meaning("FAIL");
		} else {
			syn_bar_notify_meaning("SUCCESS");
		}

	} else if (strcmp(cmd, "--set-default-sink") == 0 || strcmp(cmd, "--set-default-source") == 0) {
		if (argc != 3) { print_usage(argv[0]); rc = 1; goto done; }
		bool ok = (strcmp(cmd, "--set-default-sink") == 0)
			? syn_pulse_set_default_sink(p, argv[2], err, sizeof(err))
			: syn_pulse_set_default_source(p, argv[2], err, sizeof(err));
		if (!ok) {
			fprintf(stderr, "syn-audio: %s\n", err); rc = 1;
			syn_bar_notify_meaning("FAIL");
		} else {
			syn_bar_notify_meaning("SUCCESS");
		}

	} else if (strcmp(cmd, "--mute-sink") == 0 || strcmp(cmd, "--mute-source") == 0) {
		if (argc != 4) { print_usage(argv[0]); rc = 1; goto done; }
		bool is_sink = (strcmp(cmd, "--mute-sink") == 0);
		bool ok;
		if (strcmp(argv[3], "toggle") == 0) {
			ok = is_sink ? syn_pulse_toggle_sink_mute(p, argv[2], err, sizeof(err))
			             : syn_pulse_toggle_source_mute(p, argv[2], err, sizeof(err));
		} else if (strcmp(argv[3], "on") == 0 || strcmp(argv[3], "off") == 0) {
			bool mute = (strcmp(argv[3], "on") == 0);
			ok = is_sink ? syn_pulse_set_sink_mute(p, argv[2], mute, err, sizeof(err))
			             : syn_pulse_set_source_mute(p, argv[2], mute, err, sizeof(err));
		} else {
			print_usage(argv[0]); rc = 1; goto done;
		}
		if (!ok) {
			fprintf(stderr, "syn-audio: %s\n", err); rc = 1;
			syn_bar_notify_meaning("FAIL");
		} else if (device_is_muted(p, is_sink, argv[2])) {
			syn_bar_notify_meaning("TOGGLE_ON");
		} else {
			syn_bar_notify_meaning("TOGGLE_OFF");
		}

	} else if (strcmp(cmd, "--set-sink-volume") == 0 || strcmp(cmd, "--set-source-volume") == 0) {
		if (argc != 4) { print_usage(argv[0]); rc = 1; goto done; }
		int pct = atoi(argv[3]);
		bool ok = (strcmp(cmd, "--set-sink-volume") == 0)
			? syn_pulse_set_sink_volume(p, argv[2], pct, err, sizeof(err))
			: syn_pulse_set_source_volume(p, argv[2], pct, err, sizeof(err));
		if (!ok) { fprintf(stderr, "syn-audio: %s\n", err); rc = 1; }

	} else if (strcmp(cmd, "--adjust-sink-volume") == 0 || strcmp(cmd, "--adjust-source-volume") == 0) {
		if (argc != 4) { print_usage(argv[0]); rc = 1; goto done; }
		int delta = atoi(argv[3]);
		bool ok = (strcmp(cmd, "--adjust-sink-volume") == 0)
			? syn_pulse_adjust_sink_volume(p, argv[2], delta, err, sizeof(err))
			: syn_pulse_adjust_source_volume(p, argv[2], delta, err, sizeof(err));
		if (!ok) { fprintf(stderr, "syn-audio: %s\n", err); rc = 1; }

	} else {
		print_usage(argv[0]);
		rc = 1;
	}

done:
	syn_pulse_close(p);
	return rc;
}

static int run_interactive(void) {
	char err[256] = {0};
	syn_pulse *p = syn_pulse_open(err, sizeof(err));
	if (!p) {
		syn_audio_tui_init();
		syn_audio_tui_message("syn-audio", err);
		syn_audio_tui_end();
		return 1;
	}

	syn_audio_tui_init();

	syn_audio_tab focused = SYN_AUDIO_TAB_OUTPUTS;
	int selected = 0;

	while (1) {
		syn_pulse_device outputs[MAX_DEVICES], inputs[MAX_DEVICES];
		syn_pulse_card cards[MAX_CARDS];
		syn_audio_profile_row profile_rows[MAX_PROFILE_ROWS];
		int output_count = syn_pulse_list_sinks(p, outputs, MAX_DEVICES, err, sizeof(err));
		int input_count = syn_pulse_list_sources(p, inputs, MAX_DEVICES, err, sizeof(err));
		int card_count = syn_pulse_list_cards(p, cards, MAX_CARDS, err, sizeof(err));
		if (output_count < 0) output_count = 0;
		if (input_count < 0) input_count = 0;
		if (card_count < 0) card_count = 0;
		if (card_count > MAX_CARDS) card_count = MAX_CARDS;
		int profile_row_count = syn_audio_flatten_profiles(cards, card_count, profile_rows, MAX_PROFILE_ROWS);
		if (profile_row_count > MAX_PROFILE_ROWS) profile_row_count = MAX_PROFILE_ROWS;

		int count = (focused == SYN_AUDIO_TAB_OUTPUTS) ? output_count
			: (focused == SYN_AUDIO_TAB_INPUTS) ? input_count : profile_row_count;
		if (selected >= count) selected = count > 0 ? count - 1 : 0;
		if (selected < 0) selected = 0;

		syn_audio_input_result r = syn_audio_tui_dashboard(
			outputs, output_count, inputs, input_count,
			cards, card_count, profile_rows, profile_row_count,
			focused, selected);

		selected = r.index;

		const syn_pulse_device *list = (focused == SYN_AUDIO_TAB_OUTPUTS) ? outputs : inputs;
		bool have_selection = count > 0;

		switch (r.action) {
		case SYN_AUDIO_ACTION_QUIT:
			goto out;
		case SYN_AUDIO_ACTION_SWITCH_TAB:
			focused = (focused == SYN_AUDIO_TAB_OUTPUTS) ? SYN_AUDIO_TAB_INPUTS
				: (focused == SYN_AUDIO_TAB_INPUTS) ? SYN_AUDIO_TAB_PROFILES : SYN_AUDIO_TAB_OUTPUTS;
			selected = 0;
			break;
		case SYN_AUDIO_ACTION_SET_DEFAULT: {
			if (!have_selection) break;
			if (focused == SYN_AUDIO_TAB_PROFILES) {
				const syn_audio_profile_row *row = &profile_rows[selected];
				const syn_pulse_card *card = &cards[row->card_index];
				const syn_pulse_profile *prof = &card->profiles[row->profile_index];
				bool ok = syn_pulse_set_card_profile(p, card->name, prof->name, err, sizeof(err));
				syn_bar_notify_meaning(ok ? "SUCCESS" : "FAIL");
				break;
			}
			bool ok = (focused == SYN_AUDIO_TAB_OUTPUTS)
				? syn_pulse_set_default_sink(p, list[selected].name, err, sizeof(err))
				: syn_pulse_set_default_source(p, list[selected].name, err, sizeof(err));
			if (ok) {
				syn_bar_notify_meaning("SUCCESS");
			} else {
				syn_bar_notify_meaning("FAIL");
			}
			break;
		}
		case SYN_AUDIO_ACTION_TOGGLE_MUTE: {
			if (have_selection && focused != SYN_AUDIO_TAB_PROFILES) {
				/* list[selected].muted is the state BEFORE this toggle
				 * (captured when `list` was read, above) — a successful
				 * toggle flips it, so the new state is just its inverse.
				 * No re-query needed. */
				bool was_muted = list[selected].muted;
				bool ok = (focused == SYN_AUDIO_TAB_OUTPUTS)
					? syn_pulse_toggle_sink_mute(p, list[selected].name, err, sizeof(err))
					: syn_pulse_toggle_source_mute(p, list[selected].name, err, sizeof(err));
				if (!ok) {
					syn_bar_notify_meaning("FAIL");
				} else if (!was_muted) {
					/* now muted */
					syn_bar_notify_meaning("TOGGLE_ON");
				} else {
					/* now unmuted */
					syn_bar_notify_meaning("TOGGLE_OFF");
				}
			}
			break;
		}
		case SYN_AUDIO_ACTION_VOLUME_UP:
			if (have_selection && focused != SYN_AUDIO_TAB_PROFILES) {
				if (focused == SYN_AUDIO_TAB_OUTPUTS)
					syn_pulse_adjust_sink_volume(p, list[selected].name, 5, err, sizeof(err));
				else
					syn_pulse_adjust_source_volume(p, list[selected].name, 5, err, sizeof(err));
			}
			break;
		case SYN_AUDIO_ACTION_VOLUME_DOWN:
			if (have_selection && focused != SYN_AUDIO_TAB_PROFILES) {
				if (focused == SYN_AUDIO_TAB_OUTPUTS)
					syn_pulse_adjust_sink_volume(p, list[selected].name, -5, err, sizeof(err));
				else
					syn_pulse_adjust_source_volume(p, list[selected].name, -5, err, sizeof(err));
			}
			break;
		case SYN_AUDIO_ACTION_NONE:
		default:
			break;
		}
	}

out:
	syn_audio_tui_end();
	syn_pulse_close(p);
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 1) {
		return run_interactive();
	}
	return run_cli(argc, argv);
}
