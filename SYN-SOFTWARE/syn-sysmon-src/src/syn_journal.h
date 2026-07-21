/* ------------------------------------------------------------------------
 *   Direct sd-journal reader for syn-sysmon's log view — enumerates
 *   systemd units that have actually logged something, then streams one
 *   unit's entries live (preloaded scrollback + new entries as they
 *   arrive). No journalctl subprocess, no text parsing of its output.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_JOURNAL_H
#define SYN_JOURNAL_H

#include <stddef.h>

#define SYN_JOURNAL_MAX_UNITS 512
#define SYN_JOURNAL_UNIT_NAME_LEN 128
#define SYN_JOURNAL_LINE_LEN 512

typedef struct sd_journal sd_journal;

/* Fills `out` with every distinct _SYSTEMD_UNIT value the journal has
 * entries for (across all boots), alphabetically as returned by
 * sd_journal_query_unique — not re-sorted, that ordering is already
 * stable and good enough for a scrollable pick-list. Returns the count
 * filled, or -1 on failure to open/query the journal at all (e.g. no
 * permission to read system logs — caller should show that as an error,
 * not an empty list). */
int syn_journal_list_units(char out[][SYN_JOURNAL_UNIT_NAME_LEN], int max);

/* Opens a journal handle filtered to `unit` (NULL/"" for every unit —
 * the unscoped "full log" stream), seeks to `preload` entries before the
 * tail so the caller has immediate scrollback instead of starting empty,
 * and leaves the read position there. Returns NULL on failure. */
sd_journal *syn_journal_open(const char *unit, int preload);

void syn_journal_close(sd_journal *j);

typedef struct {
	char line[SYN_JOURNAL_LINE_LEN]; /* pre-formatted "HH:MM:SS unit: message" */
	int priority;                    /* 0 (emerg) - 7 (debug), for color */
} syn_journal_entry;

/* Reads every entry available from the current position forward (initial
 * preloaded scrollback on the first call, only newly-arrived entries on
 * later calls), up to `max` per call so one huge burst can't stall the
 * redraw loop. Returns the count filled. Never blocks — pair with
 * syn_journal_wait() in the caller's own tick loop. */
int syn_journal_read_new(sd_journal *j, syn_journal_entry *out, int max);

/* Non-blocking check for new entries becoming available — wraps
 * sd_journal_wait() with timeout_usec=0. Returns 1 if syn_journal_read_new
 * has something to read, 0 otherwise. Purely a hint: safe to skip and
 * call syn_journal_read_new directly, this just avoids the enumerate
 * overhead when nothing changed. */
int syn_journal_has_new(sd_journal *j);

#endif
