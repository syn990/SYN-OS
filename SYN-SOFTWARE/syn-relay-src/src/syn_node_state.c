/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-CLIENT (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_node_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void state_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-relay.client-state", runtime_dir);
}

bool syn_node_state_activate(void) {
	char path[512];
	state_path(path, sizeof(path));

	/* Empty file = ACTIVE (present, no host yet) — distinguished from
	 * ABSENT (fopen("r") fails entirely) by syn_node_state_get below. */
	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	fclose(f);
	return true;
}

/* File format (one connection's worth of state, four lines):
 *   1. stats-port host (what --connect actually TCP-connects to)
 *   2. ssh target/alias as typed (may differ from line 1 — see header)
 *   3. os string ("linux"/"windows"/"macos"/"" for unknown)
 *   4. capability flags, one char each in a fixed order: apps,
 *      watch_desktop, watch_window, host_watched — '1'/'0'
 * Old (pre-this-change) single-line state files simply fail the
 * has_line-on-later-fgets checks below and fall back to ACTIVE, which
 * self-heals on the next --connect — no migration needed since this
 * file lives on tmpfs and is cleared every reboot anyway. */
static void trim_newline(char *s) {
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
		s[--len] = '\0';
	}
}

bool syn_node_state_save(const char *stats_host, const char *ssh_target, const syn_node_caps *caps) {
	char path[512];
	state_path(path, sizeof(path));

	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	fprintf(f, "%s\n%s\n%s\n%c%c%c%c\n",
		stats_host, ssh_target,
		caps ? caps->os : "",
		caps && caps->apps ? '1' : '0',
		caps && caps->watch_desktop ? '1' : '0',
		caps && caps->watch_window ? '1' : '0',
		caps && caps->host_watched ? '1' : '0');
	fclose(f);
	return true;
}

syn_node_state syn_node_state_get_full(char *host_out, size_t host_out_size,
		char *ssh_target_out, size_t ssh_target_out_size, syn_node_caps *caps_out) {
	if (host_out_size > 0) {
		host_out[0] = '\0';
	}
	if (ssh_target_out && ssh_target_out_size > 0) {
		ssh_target_out[0] = '\0';
	}
	if (caps_out) {
		memset(caps_out, 0, sizeof(*caps_out));
	}

	char path[512];
	state_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return SYN_NODE_STATE_ABSENT;
	}

	char host_line[256] = {0};
	bool has_host = fgets(host_line, sizeof(host_line), f) != NULL;
	if (!has_host) {
		fclose(f);
		return SYN_NODE_STATE_ACTIVE;
	}
	trim_newline(host_line);
	if (host_line[0] == '\0') {
		fclose(f);
		return SYN_NODE_STATE_ACTIVE;
	}

	char ssh_line[256] = {0};
	if (fgets(ssh_line, sizeof(ssh_line), f)) {
		trim_newline(ssh_line);
	}
	char os_line[16] = {0};
	if (fgets(os_line, sizeof(os_line), f)) {
		trim_newline(os_line);
	}
	char caps_line[16] = {0};
	if (fgets(caps_line, sizeof(caps_line), f)) {
		trim_newline(caps_line);
	}
	fclose(f);

	snprintf(host_out, host_out_size, "%s", host_line);
	if (ssh_target_out) {
		/* Older single-line state files (or a blank ssh-target line)
		 * leave this empty — fall back to the stats host itself, which
		 * is exactly right for the common case (no ssh_config alias
		 * involved, the typed string works for both purposes). */
		snprintf(ssh_target_out, ssh_target_out_size, "%s", ssh_line[0] ? ssh_line : host_line);
	}
	if (caps_out) {
		snprintf(caps_out->os, sizeof(caps_out->os), "%s", os_line);
		caps_out->apps          = caps_line[0] == '1';
		caps_out->watch_desktop = caps_line[1] == '1';
		caps_out->watch_window  = caps_line[2] == '1';
		caps_out->host_watched  = caps_line[3] == '1';
	}
	return SYN_NODE_STATE_CONNECTED;
}

syn_node_state syn_node_state_get(char *host_out, size_t host_out_size) {
	return syn_node_state_get_full(host_out, host_out_size, NULL, 0, NULL);
}

void syn_node_state_clear(void) {
	char path[512];
	state_path(path, sizeof(path));
	remove(path);
}
