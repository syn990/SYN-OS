/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_journal.h"

#include <systemd/sd-journal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int syn_journal_list_units(char out[][SYN_JOURNAL_UNIT_NAME_LEN], int max) {
	sd_journal *j;
	if (sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY) < 0) {
		return -1;
	}

	int count = 0;
	const void *data;
	size_t length;

	if (sd_journal_query_unique(j, "_SYSTEMD_UNIT") < 0) {
		sd_journal_close(j);
		return -1;
	}

	SD_JOURNAL_FOREACH_UNIQUE(j, data, length) {
		if (count >= max) {
			break;
		}
		/* data is "FIELD=value" — skip the "_SYSTEMD_UNIT=" prefix */
		const char *eq = memchr(data, '=', length);
		if (!eq) {
			continue;
		}
		size_t val_len = length - (size_t)((const char *)eq + 1 - (const char *)data);
		if (val_len == 0 || val_len >= SYN_JOURNAL_UNIT_NAME_LEN) {
			continue;
		}
		memcpy(out[count], eq + 1, val_len);
		out[count][val_len] = '\0';
		count++;
	}

	sd_journal_close(j);
	return count;
}

sd_journal *syn_journal_open(const char *unit, int preload) {
	sd_journal *j;
	if (sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY) < 0) {
		return NULL;
	}

	if (unit && unit[0] != '\0') {
		char match[SYN_JOURNAL_UNIT_NAME_LEN + 32];
		snprintf(match, sizeof(match), "_SYSTEMD_UNIT=%s", unit);
		if (sd_journal_add_match(j, match, 0) < 0) {
			sd_journal_close(j);
			return NULL;
		}
	}

	sd_journal_seek_tail(j);
	/* seek_tail() alone leaves the read position *after* the last entry —
	 * one sd_journal_previous() first is required before the backward
	 * walk below actually lands on real entries instead of immediately
	 * running out at bof. */
	sd_journal_previous(j);
	for (int i = 0; i < preload; i++) {
		if (sd_journal_previous(j) <= 0) {
			break;
		}
	}
	return j;
}

void syn_journal_close(sd_journal *j) {
	if (j) {
		sd_journal_close(j);
	}
}

static void format_entry(sd_journal *j, syn_journal_entry *out) {
	const void *data;
	size_t length;

	char unit[SYN_JOURNAL_UNIT_NAME_LEN] = "?";
	if (sd_journal_get_data(j, "_SYSTEMD_UNIT", &data, &length) >= 0) {
		size_t prefix = strlen("_SYSTEMD_UNIT=");
		size_t val_len = length > prefix ? length - prefix : 0;
		if (val_len >= sizeof(unit)) {
			val_len = sizeof(unit) - 1;
		}
		memcpy(unit, (const char *)data + prefix, val_len);
		unit[val_len] = '\0';
	} else if (sd_journal_get_data(j, "SYSLOG_IDENTIFIER", &data, &length) >= 0) {
		size_t prefix = strlen("SYSLOG_IDENTIFIER=");
		size_t val_len = length > prefix ? length - prefix : 0;
		if (val_len >= sizeof(unit)) {
			val_len = sizeof(unit) - 1;
		}
		memcpy(unit, (const char *)data + prefix, val_len);
		unit[val_len] = '\0';
	}

	char message[SYN_JOURNAL_LINE_LEN] = "";
	if (sd_journal_get_data(j, "MESSAGE", &data, &length) >= 0) {
		size_t prefix = strlen("MESSAGE=");
		size_t val_len = length > prefix ? length - prefix : 0;
		if (val_len >= sizeof(message)) {
			val_len = sizeof(message) - 1;
		}
		memcpy(message, (const char *)data + prefix, val_len);
		message[val_len] = '\0';
	}
	/* MESSAGE can itself contain embedded newlines (multi-line stack
	 * traces etc.) — collapse to spaces so one journal entry always stays
	 * one rendered line, which the scrollback view assumes. */
	for (char *p = message; *p; p++) {
		if (*p == '\n' || *p == '\r') {
			*p = ' ';
		}
	}

	out->priority = 6; /* PRIORITY=6 (info) if the field is missing, a reasonable neutral default */
	if (sd_journal_get_data(j, "PRIORITY", &data, &length) >= 0) {
		size_t prefix = strlen("PRIORITY=");
		if (length > prefix) {
			out->priority = atoi((const char *)data + prefix);
		}
	}

	uint64_t realtime_usec = 0;
	sd_journal_get_realtime_usec(j, &realtime_usec);
	time_t seconds = (time_t)(realtime_usec / 1000000ULL);
	struct tm tm_buf;
	localtime_r(&seconds, &tm_buf);
	char time_str[16];
	strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

	snprintf(out->line, sizeof(out->line), "%s %-24.24s %s", time_str, unit, message);
}

int syn_journal_read_new(sd_journal *j, syn_journal_entry *out, int max) {
	int count = 0;
	int rc;
	while (count < max && (rc = sd_journal_next(j)) > 0) {
		format_entry(j, &out[count]);
		count++;
	}
	return count;
}

int syn_journal_has_new(sd_journal *j) {
	int rc = sd_journal_wait(j, 0);
	return rc == SD_JOURNAL_APPEND || rc == SD_JOURNAL_INVALIDATE;
}
