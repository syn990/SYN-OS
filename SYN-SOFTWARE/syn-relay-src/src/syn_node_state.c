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

bool syn_node_state_save(const char *host) {
	char path[512];
	state_path(path, sizeof(path));

	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	fprintf(f, "%s\n", host);
	fclose(f);
	return true;
}

syn_node_state syn_node_state_get(char *host_out, size_t host_out_size) {
	if (host_out_size > 0) {
		host_out[0] = '\0';
	}

	char path[512];
	state_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return SYN_NODE_STATE_ABSENT;
	}

	char line[256] = {0};
	bool has_line = fgets(line, sizeof(line), f) != NULL;
	fclose(f);

	if (!has_line) {
		return SYN_NODE_STATE_ACTIVE;
	}

	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
		line[--len] = '\0';
	}
	if (line[0] == '\0') {
		return SYN_NODE_STATE_ACTIVE;
	}

	snprintf(host_out, host_out_size, "%s", line);
	return SYN_NODE_STATE_CONNECTED;
}

void syn_node_state_clear(void) {
	char path[512];
	state_path(path, sizeof(path));
	remove(path);
}
