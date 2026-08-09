/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_sysmon_tui.h"
#include "syn_stats.h"
#include "syn_journal.h"
#include "syn_theme.h"
#include "syn_bar_client.h"
#include "syn_bar_tone.h"
#include "syn_tone_vocab.h"

#include <ncurses.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>

/* Shared refresh tick, adjustable live with [ / ] (see handle_tick_keys).
 * 100ms floor keeps CPU%% deltas (see syn_stats_cpu_usage) computed over a
 * long enough window to stay meaningful instead of reading as noise; 2000ms
 * ceiling keeps the monitor from feeling like it's stalled. ncurses'
 * timeout() is process-global, not per-window, so every view shares one
 * value rather than each keeping its own. */
static int tick_ms = 250;
#define TICK_MIN_MS 100
#define TICK_MAX_MS 2000
#define TICK_STEP_MS 50

void syn_sysmon_tui_init(void) {
	/* Same C.utf8 fallback as syn-connect's TUI: whatever launches this
	 * (waybar's on-click, doas, a bare foot -e) may not carry LANG/LC_*
	 * through, and setlocale(LC_ALL, "") silently resolves to the "C"
	 * locale in that case — which breaks ncursesw's handling of the
	 * multi-byte em-dash in "SYN-SYSMON — CPU" (renders as garbled
	 * control bytes instead of one glyph). */
	setlocale(LC_ALL, "");
	if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
		setlocale(LC_ALL, "C.utf8");
	}
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	keypad(stdscr, TRUE);
	if (has_colors()) {
		start_color();
		syn_theme_apply_curses_colors();
	}
	timeout(tick_ms); /* getch() below returns ERR on no input, driving the redraw loop */
}

void syn_sysmon_tui_end(void) {
	endwin();
}

/* Applies a [ / ] keypress to tick_ms and re-arms getch()'s timeout.
 * Returns 1 if `ch` was a tick key (caller should skip other handling for
 * this iteration), 0 otherwise. */
static int handle_tick_keys(int ch) {
	if (ch == '[') {
		tick_ms -= TICK_STEP_MS;
		if (tick_ms < TICK_MIN_MS) {
			tick_ms = TICK_MIN_MS;
		}
		timeout(tick_ms);
		return 1;
	}
	if (ch == ']') {
		tick_ms += TICK_STEP_MS;
		if (tick_ms > TICK_MAX_MS) {
			tick_ms = TICK_MAX_MS;
		}
		timeout(tick_ms);
		return 1;
	}
	return 0;
}

static const char *const TAB_NAMES[] = {"CPU", "MEM", "SENSORS", "LOGS"};
#define TAB_COUNT 4

/* Advances once per draw_header() call — the only per-frame counter every
 * view already has for free, since each view redraws once per tick. Used
 * to pulse the active tab's border so the strip reads as live rather than
 * a static label, without needing a separate timer of its own. */
static unsigned anim_frame = 0;

/* Draws the always-visible tab strip on row 0 ("[ CPU ] MEM SENSORS
 * LOGS") with `active` bordered and pulsing, the rest dim — so every
 * view is visible and reachable at a glance instead of only discoverable
 * by pressing Tab blind. Title moves to row 1, key hints to row 2. */
static void draw_header(syn_sysmon_view active) {
	int width = getmaxx(stdscr);
	anim_frame++;

	mvprintw(0, 0, "%-*s", width, "");
	int col = 2;
	for (int i = 0; i < TAB_COUNT; i++) {
		if (i == (int)active) {
			/* Pulse between the two theme pairs every ~4 ticks so the
			 * active tab visibly breathes rather than sitting static —
			 * cheap enough to just re-decide the pair each frame instead
			 * of tracking a fade state. */
			int pulse_pair = ((anim_frame / 4) % 2 == 0) ? SYN_THEME_PAIR_SELECTED : SYN_THEME_PAIR_TITLE;
			attron(COLOR_PAIR(pulse_pair) | A_BOLD);
			mvprintw(0, col, "[ %s ]", TAB_NAMES[i]);
			attroff(COLOR_PAIR(pulse_pair) | A_BOLD);
			col += (int)strlen(TAB_NAMES[i]) + 5;
		} else {
			attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
			mvprintw(0, col, "%s", TAB_NAMES[i]);
			attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));
			col += (int)strlen(TAB_NAMES[i]) + 2;
		}
	}

	attron(COLOR_PAIR(SYN_THEME_PAIR_TITLE) | A_BOLD);
	mvprintw(1, 2, "SYN-SYSMON — %s", TAB_NAMES[active]);
	attroff(COLOR_PAIR(SYN_THEME_PAIR_TITLE) | A_BOLD);

	attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
	mvprintw(2, 2, "Tab: switch view   [ / ]: refresh %dms   q/Esc: quit", tick_ms);
	attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));
}

/* Draws a `width`-wide bar at (row, col) filled to `pct` (0-100), using
 * the theme's accent/selected pair for the filled portion. Used by both
 * the CPU per-core view and the memory view so their bars look identical. */
static void draw_bar(int row, int col, int width, double pct) {
	if (pct < 0) {
		pct = 0;
	}
	if (pct > 100) {
		pct = 100;
	}
	int filled = (int)((pct / 100.0) * (double)width);

	attron(COLOR_PAIR(SYN_THEME_PAIR_SELECTED));
	for (int i = 0; i < filled; i++) {
		mvaddch(row, col + i, ' ');
	}
	attroff(COLOR_PAIR(SYN_THEME_PAIR_SELECTED));

	attron(COLOR_PAIR(SYN_THEME_PAIR_BORDER));
	for (int i = filled; i < width; i++) {
		mvaddch(row, col + i, ' ');
	}
	attroff(COLOR_PAIR(SYN_THEME_PAIR_BORDER));

	char pct_label[8];
	snprintf(pct_label, sizeof(pct_label), "%3.0f%%", pct);
	int label_col = col + width + 1;
	mvprintw(row, label_col, "%s", pct_label);
}

/* Row 3 (below the header's key-hints line): "LOCAL" when syn-bar-core
 * is unreachable or not relay-connected, "REMOTE: <host>" when it's
 * handing back a connected SYN-RELAY node's stats, or a waiting note
 * while relay is active but not yet connected. */
static void draw_source_row(syn_bar_source source, const char *host) {
	attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
	if (source == SYN_BAR_SOURCE_REMOTE) {
		mvprintw(3, 2, "REMOTE: %s", host);
	} else if (source == SYN_BAR_SOURCE_WAITING) {
		mvprintw(3, 2, "SYN-RELAY active, not connected — showing local stats");
	} else {
		mvprintw(3, 2, "LOCAL");
	}
	attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));
}

/* Sensors (hwmon) and Logs (systemd journal) have no remote equivalent —
 * syn_relay_protocol.h's STATS reply carries no temperature or journal
 * field, so there's nothing to fetch even in principle today. Unlike
 * CPU/RAM (which genuinely switch to a connected node's live numbers,
 * see draw_source_row above), silently rendering THIS machine's own
 * sensors/journal while relay is connected — with no on-screen
 * indication that's what happened — would be actively misleading: it
 * looks like "the remote's temps/logs" with nothing to tell you
 * otherwise. Reuses SYSMON-CPU purely to read the cached connection
 * state (cheap — syn-bar-core answers from its own poll cache, no
 * per-call network fetch) rather than adding a second verb just to ask
 * "are we connected." */
static bool remote_connected(char *host_out, size_t host_out_size) {
	syn_bar_cpu_reply remote;
	if (!syn_bar_client_cpu(&remote) || remote.source != SYN_BAR_SOURCE_REMOTE) {
		return false;
	}
	if (host_out && host_out_size > 0) {
		snprintf(host_out, host_out_size, "%s", remote.host);
	}
	return true;
}

static void draw_remote_unavailable_placeholder(const char *host) {
	attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
	mvprintw(3, 2, "REMOTE: %s", host);
	attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));
	mvprintw(5, 2, "Not available for remote nodes — this view stays local-only for now.");
}

static int run_cpu_view(void) {
	syn_cpu_snapshot prev, cur;
	syn_stats_cpu_read(&prev);

	while (1) {
		int ch = getch();
		if (ch == 'q' || ch == 27) {
			return -1;
		}
		if (ch == '\t') {
			return SYN_SYSMON_VIEW_MEM;
		}
		handle_tick_keys(ch);

		syn_bar_cpu_reply remote;
		bool have_remote = syn_bar_client_cpu(&remote)
			&& remote.source == SYN_BAR_SOURCE_REMOTE;

		erase();
		draw_header(SYN_SYSMON_VIEW_CPU);
		draw_source_row(have_remote ? SYN_BAR_SOURCE_REMOTE : SYN_BAR_SOURCE_LOCAL, remote.host);

		int width = getmaxx(stdscr);
		int bar_width = width > 40 ? width - 20 : 20;

		if (have_remote) {
			attron(A_BOLD);
			mvprintw(5, 2, "Aggregate");
			attroff(A_BOLD);
			draw_bar(5, 14, bar_width, remote.total_pct);
			/* Keep prev fresh while showing remote so the first local
			 * reading after switching back isn't diffed against a
			 * snapshot that's been stale for the whole remote session. */
			syn_stats_cpu_read(&prev);
		} else {
			syn_stats_cpu_read(&cur);
			double total_pct = syn_stats_cpu_usage(&prev, &cur, -1);
			attron(A_BOLD);
			mvprintw(5, 2, "Aggregate");
			attroff(A_BOLD);
			draw_bar(5, 14, bar_width, total_pct);

			int row = 7;
			for (int i = 0; i < cur.core_count && row < getmaxy(stdscr) - 1; i++) {
				double pct = syn_stats_cpu_usage(&prev, &cur, i);
				mvprintw(row, 2, "core%-2d", i);
				draw_bar(row, 14, bar_width, pct);
				row++;
			}
			prev = cur;
		}

		refresh();
	}
}

static void draw_kb_row(int row, const char *label, unsigned long long kb) {
	mvprintw(row, 2, "%-14s %8.1f GiB", label, (double)kb / (1024.0 * 1024.0));
}

static int run_mem_view(void) {
	while (1) {
		int ch = getch();
		if (ch == 'q' || ch == 27) {
			return -1;
		}
		if (ch == '\t') {
			return SYN_SYSMON_VIEW_SENSORS;
		}
		handle_tick_keys(ch);

		syn_bar_mem_reply remote;
		bool have_remote = syn_bar_client_mem(&remote)
			&& remote.source == SYN_BAR_SOURCE_REMOTE;

		erase();
		draw_header(SYN_SYSMON_VIEW_MEM);
		draw_source_row(have_remote ? SYN_BAR_SOURCE_REMOTE : SYN_BAR_SOURCE_LOCAL, remote.host);

		int width = getmaxx(stdscr);
		int bar_width = width > 40 ? width - 20 : 20;

		if (have_remote) {
			double used_pct = remote.total_kb ? (double)remote.used_kb * 100.0 / (double)remote.total_kb : 0.0;
			attron(A_BOLD);
			mvprintw(5, 2, "RAM used");
			attroff(A_BOLD);
			draw_bar(5, 14, bar_width, used_pct);

			draw_kb_row(7, "Total", remote.total_kb);
			draw_kb_row(8, "Used", remote.used_kb);
		} else {
			syn_mem_snapshot mem;
			syn_stats_mem_read(&mem);

			unsigned long long used_kb = mem.total_kb > mem.available_kb ? mem.total_kb - mem.available_kb : 0;
			double used_pct = mem.total_kb ? (double)used_kb * 100.0 / (double)mem.total_kb : 0.0;

			attron(A_BOLD);
			mvprintw(5, 2, "RAM used");
			attroff(A_BOLD);
			draw_bar(5, 14, bar_width, used_pct);

			draw_kb_row(7, "Total", mem.total_kb);
			draw_kb_row(8, "Used", used_kb);
			draw_kb_row(9, "Available", mem.available_kb);
			draw_kb_row(10, "Cached", mem.cached_kb);
			draw_kb_row(11, "Buffers", mem.buffers_kb);

			if (mem.swap_total_kb > 0) {
				unsigned long long swap_used = mem.swap_total_kb > mem.swap_free_kb ? mem.swap_total_kb - mem.swap_free_kb : 0;
				double swap_pct = (double)swap_used * 100.0 / (double)mem.swap_total_kb;
				attron(A_BOLD);
				mvprintw(13, 2, "Swap used");
				attroff(A_BOLD);
				draw_bar(13, 14, bar_width, swap_pct);
				draw_kb_row(15, "Swap total", mem.swap_total_kb);
			} else {
				mvprintw(13, 2, "No swap configured");
			}
		}

		refresh();
	}
}

static int run_sensors_view(void) {
	while (1) {
		int ch = getch();
		if (ch == 'q' || ch == 27) {
			return -1;
		}
		if (ch == '\t') {
			return SYN_SYSMON_VIEW_LOGS;
		}
		handle_tick_keys(ch);

		erase();
		draw_header(SYN_SYSMON_VIEW_SENSORS);

		char host[256];
		if (remote_connected(host, sizeof(host))) {
			draw_remote_unavailable_placeholder(host);
			refresh();
			continue;
		}

		syn_sensor_reading readings[SYN_STATS_MAX_SENSORS];
		int count = syn_stats_sensors_read(readings, SYN_STATS_MAX_SENSORS);

		bool any_hot = false;
		int row = 4;
		for (int i = 0; i < count && row < getmaxy(stdscr) - 1; i++) {
			int pair = SYN_THEME_PAIR_NORMAL;
			if (readings[i].celsius >= 90.0) {
				pair = SYN_THEME_PAIR_TITLE; /* reuse accent-colored pair as the "hot" warning */
				any_hot = true;
			}
			attron(COLOR_PAIR(pair));
			mvprintw(row, 2, "%-32s %5.1f C", readings[i].label, readings[i].celsius);
			attroff(COLOR_PAIR(pair));
			row++;
		}
		if (count == 0) {
			mvprintw(4, 2, "No hwmon sensors found");
		}

		/* Edge-detected, not fired every redraw tick while still hot —
		 * same was_fresh/was_watching shape syn-bar-core's own main.c
		 * uses for its transition-tracked tones. One sensor crossing
		 * 90C is enough to trip this (not "all of them"), and it resets
		 * the moment none are hot anymore so a second crossing later in
		 * the same session re-alerts. */
		static bool was_hot = false;
		if (any_hot && !was_hot) {
			syn_bar_tone_play_dtmf(SYN_TONE_ALERT_LOW, SYN_TONE_ALERT_HIGH, SYN_TONE_ALERT_SECONDS);
		}
		was_hot = any_hot;

		refresh();
	}
}

/* Color pair for a journal PRIORITY value (0=emerg .. 7=debug): warning-and-
 * above gets the theme's urgent-toned title pair, everything else is
 * normal text — matches how the sensors view already treats "hot" as the
 * one thing worth calling out rather than color-coding every level. */
static int priority_pair(int priority) {
	return priority <= 4 ? SYN_THEME_PAIR_TITLE : SYN_THEME_PAIR_NORMAL;
}

#define LOG_SCROLLBACK_MAX 2000

typedef struct {
	syn_journal_entry entries[LOG_SCROLLBACK_MAX];
	int count;       /* entries actually filled, caps at LOG_SCROLLBACK_MAX */
	int next_slot;    /* ring write cursor once count == LOG_SCROLLBACK_MAX */
	int scroll_offset; /* 0 = pinned to newest (auto-follow); >0 = scrolled back that many lines */
} log_ring;

static void log_ring_push(log_ring *ring, const syn_journal_entry *e) {
	if (ring->count < LOG_SCROLLBACK_MAX) {
		ring->entries[ring->count++] = *e;
	} else {
		ring->entries[ring->next_slot] = *e;
		ring->next_slot = (ring->next_slot + 1) % LOG_SCROLLBACK_MAX;
	}
}

/* Logical index 0..count-1, oldest to newest, hiding the ring's physical
 * wraparound from every caller below. */
static const syn_journal_entry *log_ring_at(const log_ring *ring, int logical_idx) {
	if (ring->count < LOG_SCROLLBACK_MAX) {
		return &ring->entries[logical_idx];
	}
	int physical = (ring->next_slot + logical_idx) % LOG_SCROLLBACK_MAX;
	return &ring->entries[physical];
}

typedef enum {
	PICK_UNIT_CHOSE, /* a real unit (or "" for the unscoped stream) was chosen */
	PICK_UNIT_QUIT,  /* Esc/q */
	PICK_UNIT_TAB,   /* Tab — cycle onward to CPU without picking a unit first */
} pick_unit_outcome;

/* Unit picker: a scrollable list of every _SYSTEMD_UNIT the journal has
 * entries for, plus a synthetic "(all units — full log)" entry at the
 * top for the unscoped stream. *outcome tells the caller which of the
 * three ways this returned; the return value is only meaningful (and
 * only a pointer into `units`/a static "") when *outcome is
 * PICK_UNIT_CHOSE. */
static const char *pick_unit(char units[][SYN_JOURNAL_UNIT_NAME_LEN], int unit_count, int *selected, pick_unit_outcome *outcome) {
	int top = 0; /* first visible row, for scrolling a list longer than the screen */

	while (1) {
		erase();
		draw_header(SYN_SYSMON_VIEW_LOGS);
		attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
		mvprintw(3, 2, "Choose a unit (Enter to stream, arrows to move):");
		attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));

		int height = getmaxy(stdscr);
		int visible_rows = height - 5;
		int total = unit_count + 1; /* +1 for the synthetic "all units" row */

		if (*selected < top) {
			top = *selected;
		}
		if (*selected >= top + visible_rows) {
			top = *selected - visible_rows + 1;
		}

		for (int row = 0; row < visible_rows && top + row < total; row++) {
			int idx = top + row;
			int is_selected = (idx == *selected);
			const char *label = (idx == 0) ? "(all units \xe2\x80\x94 full log)" : units[idx - 1];

			if (is_selected) {
				attron(COLOR_PAIR(SYN_THEME_PAIR_SELECTED));
			}
			mvprintw(4 + row, 2, "%-*s", getmaxx(stdscr) - 4, label);
			if (is_selected) {
				attroff(COLOR_PAIR(SYN_THEME_PAIR_SELECTED));
			}
		}
		refresh();

		int ch = getch();
		if (ch == 'q' || ch == 27) {
			*outcome = PICK_UNIT_QUIT;
			return NULL;
		}
		if (ch == '\t') {
			*outcome = PICK_UNIT_TAB;
			return NULL;
		}
		if (ch == KEY_UP && *selected > 0) {
			(*selected)--;
		} else if (ch == KEY_DOWN && *selected < total - 1) {
			(*selected)++;
		} else if (ch == '\n' || ch == KEY_ENTER) {
			*outcome = PICK_UNIT_CHOSE;
			return (*selected == 0) ? "" : units[*selected - 1];
		}
	}
}

/* Streams `unit`'s journal entries live (preloaded scrollback + new
 * entries as they arrive each tick), with PgUp/PgDn/arrow scrolling back
 * into history without interrupting the live stream underneath. Esc/q
 * returns to the unit picker rather than quitting the whole app — logs
 * is the one view with two internal screens, so its own back-navigation
 * takes priority over the usual Tab/quit-only scheme. Returns 1 to go
 * back to the picker, -1 to quit entirely, or SYN_SYSMON_VIEW_CPU on Tab. */
static int stream_unit(const char *unit) {
	sd_journal *j = syn_journal_open(unit, 200);
	log_ring ring = {0};

	if (!j) {
		erase();
		draw_header(SYN_SYSMON_VIEW_LOGS);
		mvprintw(4, 2, "Failed to open journal (permission denied?)");
		refresh();
		int ch = getch();
		return (ch == '\t') ? SYN_SYSMON_VIEW_CPU : 1;
	}

	syn_journal_entry buf[128];
	int n = syn_journal_read_new(j, buf, 128);
	for (int i = 0; i < n; i++) {
		log_ring_push(&ring, &buf[i]);
	}

	int result = 1;
	while (1) {
		int ch = getch();
		if (ch == 'q' || ch == 27) {
			result = 1;
			break;
		}
		if (ch == '\t') {
			result = SYN_SYSMON_VIEW_CPU;
			break;
		}
		if (!handle_tick_keys(ch)) {
			if (ch == KEY_UP && ring.scroll_offset < ring.count - 1) {
				ring.scroll_offset++;
			} else if (ch == KEY_DOWN && ring.scroll_offset > 0) {
				ring.scroll_offset--;
			} else if (ch == KEY_PPAGE) {
				ring.scroll_offset += 10;
				if (ring.scroll_offset > ring.count - 1) {
					ring.scroll_offset = ring.count > 0 ? ring.count - 1 : 0;
				}
			} else if (ch == KEY_NPAGE) {
				ring.scroll_offset -= 10;
				if (ring.scroll_offset < 0) {
					ring.scroll_offset = 0;
				}
			} else if (ch == 'g') {
				ring.scroll_offset = ring.count > 0 ? ring.count - 1 : 0; /* jump to oldest loaded */
			} else if (ch == 'G') {
				ring.scroll_offset = 0; /* jump back to live tail */
			}
		}

		if (syn_journal_has_new(j)) {
			n = syn_journal_read_new(j, buf, 128);
			for (int i = 0; i < n; i++) {
				log_ring_push(&ring, &buf[i]);
			}
			/* Entries arriving while scrolled back would otherwise shove
			 * the view forward under the user's feet — hold position by
			 * growing the offset by the same amount just appended. */
			if (ring.scroll_offset > 0) {
				ring.scroll_offset += n;
			}
		}

		erase();
		draw_header(SYN_SYSMON_VIEW_LOGS);
		attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
		mvprintw(3, 2, "Streaming: %s", (unit && unit[0]) ? unit : "all units");
		attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));

		int height = getmaxy(stdscr);
		int visible_rows = height - 5;
		int width = getmaxx(stdscr);

		int last_idx = ring.count - 1 - ring.scroll_offset; /* newest line to show at the bottom */
		for (int row = 0; row < visible_rows && row <= last_idx; row++) {
			int idx = last_idx - row;
			if (idx < 0) {
				break;
			}
			const syn_journal_entry *e = log_ring_at(&ring, idx);
			int pair = priority_pair(e->priority);
			attron(COLOR_PAIR(pair));
			mvprintw(height - 1 - row, 2, "%-*.*s", width - 4, width - 4, e->line);
			attroff(COLOR_PAIR(pair));
		}

		if (ring.scroll_offset > 0) {
			attron(COLOR_PAIR(SYN_THEME_PAIR_DIM));
			mvprintw(3, width > 50 ? 40 : width - 10, "-- scrolled back %d, G for live --", ring.scroll_offset);
			attroff(COLOR_PAIR(SYN_THEME_PAIR_DIM));
		}

		refresh();
	}

	syn_journal_close(j);
	return result;
}

/* Same "don't silently show the wrong machine's data" rule as Sensors
 * (see remote_connected()/draw_remote_unavailable_placeholder() above) —
 * the journal is inherently local, so this blocks entry to the unit
 * picker entirely rather than letting it stream this machine's own log
 * while relay is connected. Re-checks on a normal tick so disconnecting
 * while parked here recovers into the real picker without restarting
 * the view. */
static int run_logs_view_remote_placeholder(void) {
	while (1) {
		char host[256];
		if (!remote_connected(host, sizeof(host))) {
			return 0; /* connection dropped — fall through to the real picker */
		}

		erase();
		draw_header(SYN_SYSMON_VIEW_LOGS);
		draw_remote_unavailable_placeholder(host);
		refresh();

		timeout(tick_ms);
		int ch = getch();
		if (ch == 'q' || ch == 27) {
			return -1;
		}
		if (ch == '\t') {
			return SYN_SYSMON_VIEW_CPU;
		}
		handle_tick_keys(ch);
	}
}

static int run_logs_view(void) {
	static char units[SYN_JOURNAL_MAX_UNITS][SYN_JOURNAL_UNIT_NAME_LEN];
	static int unit_count = -1; /* -1 = not loaded yet; loaded once per process, units rarely change mid-session */
	static int selected = 0;

	if (unit_count < 0) {
		unit_count = syn_journal_list_units(units, SYN_JOURNAL_MAX_UNITS);
		if (unit_count < 0) {
			unit_count = 0;
		}
	}

	while (1) {
		int placeholder_result = run_logs_view_remote_placeholder();
		if (placeholder_result != 0) {
			return placeholder_result; /* -1 (quit) or a view index (Tab) */
		}

		pick_unit_outcome outcome;
		const char *chosen = pick_unit(units, unit_count, &selected, &outcome);
		if (outcome == PICK_UNIT_QUIT) {
			return -1; /* Esc/q from the picker quits the app, same as every other view */
		}
		if (outcome == PICK_UNIT_TAB) {
			return SYN_SYSMON_VIEW_CPU; /* completes the cycle even before a unit is chosen */
		}

		int result = stream_unit(chosen);
		if (result != 1) {
			return result; /* Tab (switch view) or an open failure propagates out */
		}
		/* result == 1: Esc/q from the stream — loop back to the picker */
	}
}

int syn_sysmon_tui_run(syn_sysmon_view view) {
	switch (view) {
	case SYN_SYSMON_VIEW_CPU:
		return run_cpu_view();
	case SYN_SYSMON_VIEW_MEM:
		return run_mem_view();
	case SYN_SYSMON_VIEW_SENSORS:
		return run_sensors_view();
	case SYN_SYSMON_VIEW_LOGS:
		return run_logs_view();
	}
	return -1;
}
