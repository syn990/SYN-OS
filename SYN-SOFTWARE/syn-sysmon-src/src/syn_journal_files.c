/* ------------------------------------------------------------------------
 *   Plain log file backend for syn-sysmon's log view, for systems with no
 *   systemd journal (SYN-OS on runit). Same interface as syn_journal.c;
 *   CMakeLists.txt builds this one when libsystemd isn't there.
 *
 *   A "unit" is one log file: every runit service's svlogd log
 *   (/var/log/sv/NAME/current, named NAME) and syslogd's files
 *   (/var/log/NAME.log, named NAME.log). Lines are read as they are
 *   appended, and svlogd's rotation (current renamed, a new one started)
 *   is followed. There's no priority field in a text log, so a line that
 *   mentions an error or a warning gets that priority's colour.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _GNU_SOURCE
#include "syn_journal.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef SV_LOG_DIR
#define SV_LOG_DIR "/var/log/sv"
#endif
#ifndef SYSLOG_DIR
#define SYSLOG_DIR "/var/log"
#endif

typedef struct {
	char unit[SYN_JOURNAL_UNIT_NAME_LEN];
	char path[PATH_MAX];
	int fd;
	ino_t ino;
	char partial[SYN_JOURNAL_LINE_LEN]; /* a line still being written */
	size_t partial_len;
} log_source;

typedef struct {
	time_t when;
	syn_journal_entry entry;
} timed_entry;

/* The header's opaque handle type, defined here for this backend */
struct sd_journal {
	log_source *src;
	int n;
	timed_entry *backlog; /* lines read but not handed out yet */
	int backlog_n, backlog_pos, backlog_cap;
};

static int by_name(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

/* Every readable log, as unit name + path, sorted by name */
static int find_sources(char names[][SYN_JOURNAL_UNIT_NAME_LEN], char paths[][PATH_MAX], int max) {
	struct {
		char name[SYN_JOURNAL_UNIT_NAME_LEN];
		char path[PATH_MAX];
	} *found = calloc((size_t)max, sizeof(*found));
	if (!found) {
		return -1;
	}
	int n = 0, dirs_opened = 0;
	DIR *d = opendir(SV_LOG_DIR);
	if (d) {
		dirs_opened++;
		struct dirent *e;
		while (n < max && (e = readdir(d))) {
			if (e->d_name[0] == '.' || strlen(e->d_name) >= SYN_JOURNAL_UNIT_NAME_LEN) {
				continue;
			}
			snprintf(found[n].path, PATH_MAX, "%s/%s/current", SV_LOG_DIR, e->d_name);
			if (access(found[n].path, R_OK) == 0) {
				snprintf(found[n].name, SYN_JOURNAL_UNIT_NAME_LEN, "%s", e->d_name);
				n++;
			}
		}
		closedir(d);
	}
	d = opendir(SYSLOG_DIR);
	if (d) {
		dirs_opened++;
		struct dirent *e;
		while (n < max && (e = readdir(d))) {
			size_t len = strlen(e->d_name);
			if (len < 5 || len >= SYN_JOURNAL_UNIT_NAME_LEN || strcmp(e->d_name + len - 4, ".log") != 0) {
				continue;
			}
			snprintf(found[n].path, PATH_MAX, "%s/%s", SYSLOG_DIR, e->d_name);
			struct stat st;
			if (stat(found[n].path, &st) == 0 && S_ISREG(st.st_mode) && access(found[n].path, R_OK) == 0) {
				snprintf(found[n].name, SYN_JOURNAL_UNIT_NAME_LEN, "%s", e->d_name);
				n++;
			}
		}
		closedir(d);
	}
	qsort(found, (size_t)n, sizeof(*found), by_name);
	for (int i = 0; i < n; i++) {
		memcpy(names[i], found[i].name, SYN_JOURNAL_UNIT_NAME_LEN);
		if (paths) {
			memcpy(paths[i], found[i].path, PATH_MAX);
		}
	}
	free(found);
	return dirs_opened ? n : -1;
}

int syn_journal_list_units(char out[][SYN_JOURNAL_UNIT_NAME_LEN], int max) {
	return find_sources(out, NULL, max);
}

static const char *const month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* Splits a raw line into its time and message. svlogd -tt lines start
 * "2026-09-22_15:22:01.12345 " (UTC); syslog lines start "Sep 22 15:22:01
 * host " (local time, no year). Anything else keeps the whole line and
 * gets time 0. */
static const char *parse_time(const char *line, time_t *when) {
	struct tm tm = {0};
	int consumed = 0;
	char mon[4];
	*when = 0;
	if (sscanf(line, "%4d-%2d-%2d_%2d:%2d:%2d%n", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
	           &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &consumed) == 6) {
		tm.tm_year -= 1900;
		tm.tm_mon -= 1;
		*when = timegm(&tm);
		const char *p = line + consumed;
		while (*p && *p != ' ') {
			p++; /* the fractional seconds */
		}
		return *p ? p + 1 : p;
	}
	if (sscanf(line, "%3s %2d %2d:%2d:%2d%n", mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
	           &tm.tm_sec, &consumed) == 5) {
		for (int m = 0; m < 12; m++) {
			if (strcmp(mon, month_names[m]) == 0) {
				time_t now = time(NULL);
				struct tm now_tm;
				localtime_r(&now, &now_tm);
				tm.tm_mon = m;
				tm.tm_year = now_tm.tm_year;
				tm.tm_isdst = -1;
				*when = mktime(&tm);
				if (*when > now + 86400) { /* December's lines read in January */
					tm.tm_year--;
					*when = mktime(&tm);
				}
				const char *p = line + consumed;
				while (*p == ' ') {
					p++;
				}
				while (*p && *p != ' ') {
					p++; /* the host name */
				}
				return *p ? p + 1 : p;
			}
		}
	}
	return line;
}

static int guess_priority(const char *msg) {
	if (strcasestr(msg, "crit") || strcasestr(msg, "fatal") || strcasestr(msg, "panic")) {
		return 2;
	}
	if (strcasestr(msg, "error") || strcasestr(msg, "fail")) {
		return 3;
	}
	if (strcasestr(msg, "warn")) {
		return 4;
	}
	return 6;
}

static void format_line(const char *unit, const char *raw, timed_entry *out) {
	const char *msg = parse_time(raw, &out->when);
	char time_str[16] = "--:--:--";
	if (out->when) {
		struct tm tm_buf;
		localtime_r(&out->when, &tm_buf);
		strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);
	}
	out->entry.priority = guess_priority(msg);
	snprintf(out->entry.line, sizeof(out->entry.line), "%s %-24.24s %s", time_str, unit, msg);
}

/* Reads whatever has been appended to one source since the last call and
 * hands each complete line to `emit`. When the file has been rotated, the
 * rest of the old one is read first, then the new one from its start. */
typedef void (*line_fn)(void *ctx, const char *unit, const char *line);

static void drain(log_source *s, line_fn emit, void *ctx) {
	for (int pass = 0; pass < 2; pass++) {
		if (s->fd >= 0) {
			char buf[8192];
			ssize_t got;
			while ((got = read(s->fd, buf, sizeof(buf))) > 0) {
				for (ssize_t i = 0; i < got; i++) {
					if (buf[i] == '\n') {
						s->partial[s->partial_len] = '\0';
						emit(ctx, s->unit, s->partial);
						s->partial_len = 0;
					} else if (s->partial_len < sizeof(s->partial) - 1) {
						s->partial[s->partial_len++] = buf[i] == '\r' ? ' ' : buf[i];
					}
				}
			}
		}
		struct stat st;
		if (stat(s->path, &st) != 0 || (s->fd >= 0 && st.st_ino == s->ino)) {
			return;
		}
		if (s->fd >= 0) {
			close(s->fd);
		}
		s->fd = open(s->path, O_RDONLY | O_CLOEXEC);
		s->ino = st.st_ino;
		s->partial_len = 0;
	}
}

/* The preload pass keeps only each source's last `keep` lines */
typedef struct {
	timed_entry *ring;
	int keep, count;
} tail_ctx;

static void keep_tail(void *ctx, const char *unit, const char *line) {
	tail_ctx *t = ctx;
	format_line(unit, line, &t->ring[t->count % t->keep]);
	t->count++;
}

static int by_time(const void *a, const void *b) {
	time_t ta = ((const timed_entry *)a)->when, tb = ((const timed_entry *)b)->when;
	return (ta > tb) - (ta < tb);
}

sd_journal *syn_journal_open(const char *unit, int preload) {
	static char names[SYN_JOURNAL_MAX_UNITS][SYN_JOURNAL_UNIT_NAME_LEN];
	static char paths[SYN_JOURNAL_MAX_UNITS][PATH_MAX];
	int total = find_sources(names, paths, SYN_JOURNAL_MAX_UNITS);
	if (total < 0) {
		return NULL;
	}
	sd_journal *j = calloc(1, sizeof(*j));
	if (!j) {
		return NULL;
	}
	j->src = calloc((size_t)(total ? total : 1), sizeof(log_source));
	int keep = preload > 0 ? preload : 1;
	j->backlog_cap = keep * (total ? total : 1);
	j->backlog = calloc((size_t)j->backlog_cap, sizeof(timed_entry));
	timed_entry *ring = calloc((size_t)keep, sizeof(timed_entry));
	if (!j->src || !j->backlog || !ring) {
		free(ring);
		syn_journal_close(j);
		return NULL;
	}

	for (int i = 0; i < total; i++) {
		if (unit && unit[0] && strcmp(unit, names[i]) != 0) {
			continue;
		}
		log_source *s = &j->src[j->n++];
		memcpy(s->unit, names[i], sizeof(s->unit));
		memcpy(s->path, paths[i], sizeof(s->path));
		s->fd = -1;
		tail_ctx t = {ring, keep, 0};
		drain(s, keep_tail, &t);
		int have = t.count < keep ? t.count : keep;
		for (int k = 0; k < have; k++) {
			j->backlog[j->backlog_n++] = ring[(t.count - have + k) % keep];
		}
	}
	free(ring);
	if (unit && unit[0] && j->n == 0) {
		syn_journal_close(j);
		return NULL;
	}

	/* Several logs interleave by time; then only the newest `preload` */
	qsort(j->backlog, (size_t)j->backlog_n, sizeof(timed_entry), by_time);
	if (j->backlog_n > keep) {
		j->backlog_pos = j->backlog_n - keep;
	}
	return j;
}

void syn_journal_close(sd_journal *j) {
	if (!j) {
		return;
	}
	for (int i = 0; i < j->n; i++) {
		if (j->src[i].fd >= 0) {
			close(j->src[i].fd);
		}
	}
	free(j->src);
	free(j->backlog);
	free(j);
}

/* New lines queue up in the backlog, so a burst bigger than one call's
 * `max` waits for the next call instead of being dropped */
static void queue_line(void *ctx, const char *unit, const char *line) {
	sd_journal *j = ctx;
	if (j->backlog_pos > 0 && j->backlog_pos == j->backlog_n) {
		j->backlog_pos = j->backlog_n = 0;
	}
	if (j->backlog_n == j->backlog_cap) {
		int cap = j->backlog_cap ? j->backlog_cap * 2 : 256;
		timed_entry *grown = realloc(j->backlog, (size_t)cap * sizeof(timed_entry));
		if (!grown) {
			return;
		}
		j->backlog = grown;
		j->backlog_cap = cap;
	}
	format_line(unit, line, &j->backlog[j->backlog_n++]);
}

int syn_journal_read_new(sd_journal *j, syn_journal_entry *out, int max) {
	for (int i = 0; i < j->n; i++) {
		drain(&j->src[i], queue_line, j);
	}
	int count = 0;
	while (count < max && j->backlog_pos < j->backlog_n) {
		out[count++] = j->backlog[j->backlog_pos++].entry;
	}
	return count;
}

int syn_journal_has_new(sd_journal *j) {
	if (j->backlog_pos < j->backlog_n) {
		return 1;
	}
	for (int i = 0; i < j->n; i++) {
		log_source *s = &j->src[i];
		struct stat st;
		if (stat(s->path, &st) != 0) {
			continue;
		}
		if (s->fd < 0 || st.st_ino != s->ino || st.st_size > lseek(s->fd, 0, SEEK_CUR)) {
			return 1;
		}
	}
	return 0;
}
