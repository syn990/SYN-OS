/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_agent_state.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runtime_path(const char *filename, char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/%s", runtime_dir, filename);
}

static bool pid_alive(pid_t pid) {
	return pid > 0 && kill(pid, 0) == 0;
}

static void strip_eol(char *s) {
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
		s[--len] = '\0';
	}
}

/* ---- watch role ---- */

bool syn_watch_state_save(pid_t view_pid, const char *remote_ip) {
	char pid_path[512], host_path[512];
	runtime_path("syn-relay.watch.pid", pid_path, sizeof(pid_path));
	runtime_path("syn-relay.watch.host", host_path, sizeof(host_path));

	FILE *f = fopen(pid_path, "w");
	if (!f) {
		return false;
	}
	fprintf(f, "%d\n", (int)view_pid);
	fclose(f);

	f = fopen(host_path, "w");
	if (!f) {
		remove(pid_path);
		return false;
	}
	fprintf(f, "%s\n", remote_ip);
	fclose(f);
	return true;
}

pid_t syn_watch_state_get(char *host_out, size_t host_out_size) {
	if (host_out_size > 0) {
		host_out[0] = '\0';
	}

	char pid_path[512];
	runtime_path("syn-relay.watch.pid", pid_path, sizeof(pid_path));
	FILE *f = fopen(pid_path, "r");
	if (!f) {
		return 0;
	}
	long pid_val = 0;
	bool ok = fscanf(f, "%ld", &pid_val) == 1;
	fclose(f);
	if (!ok || !pid_alive((pid_t)pid_val)) {
		return 0;
	}

	char host_path[512];
	runtime_path("syn-relay.watch.host", host_path, sizeof(host_path));
	f = fopen(host_path, "r");
	if (f) {
		if (fgets(host_out, (int)host_out_size, f)) {
			strip_eol(host_out);
		}
		fclose(f);
	}
	return (pid_t)pid_val;
}

void syn_watch_state_clear(void) {
	char pid_path[512], host_path[512];
	runtime_path("syn-relay.watch.pid", pid_path, sizeof(pid_path));
	runtime_path("syn-relay.watch.host", host_path, sizeof(host_path));
	remove(pid_path);
	remove(host_path);
}

/* ---- host role ---- */

bool syn_host_state_save(pid_t video_pid, pid_t input_pid, const char *viewer_ip) {
	char pid_path[512], viewer_path[512];
	runtime_path("syn-relay.host.pid", pid_path, sizeof(pid_path));
	runtime_path("syn-relay.host.viewer", viewer_path, sizeof(viewer_path));

	FILE *f = fopen(pid_path, "w");
	if (!f) {
		return false;
	}
	fprintf(f, "%d\n%d\n", (int)video_pid, (int)input_pid);
	fclose(f);

	f = fopen(viewer_path, "w");
	if (!f) {
		remove(pid_path);
		return false;
	}
	fprintf(f, "%s\n", viewer_ip);
	fclose(f);
	return true;
}

bool syn_host_state_get(pid_t *video_pid_out, pid_t *input_pid_out,
		char *viewer_out, size_t viewer_out_size) {
	*video_pid_out = 0;
	*input_pid_out = 0;
	if (viewer_out_size > 0) {
		viewer_out[0] = '\0';
	}

	char pid_path[512];
	runtime_path("syn-relay.host.pid", pid_path, sizeof(pid_path));
	FILE *f = fopen(pid_path, "r");
	if (!f) {
		return false;
	}
	long video_val = 0, input_val = 0;
	bool ok = fscanf(f, "%ld %ld", &video_val, &input_val) == 2;
	fclose(f);
	if (!ok || !pid_alive((pid_t)video_val) || !pid_alive((pid_t)input_val)) {
		return false;
	}

	char viewer_path[512];
	runtime_path("syn-relay.host.viewer", viewer_path, sizeof(viewer_path));
	f = fopen(viewer_path, "r");
	if (f) {
		if (fgets(viewer_out, (int)viewer_out_size, f)) {
			strip_eol(viewer_out);
		}
		fclose(f);
	}

	*video_pid_out = (pid_t)video_val;
	*input_pid_out = (pid_t)input_val;
	return true;
}

void syn_host_state_clear(void) {
	char pid_path[512], viewer_path[512];
	runtime_path("syn-relay.host.pid", pid_path, sizeof(pid_path));
	runtime_path("syn-relay.host.viewer", viewer_path, sizeof(viewer_path));
	remove(pid_path);
	remove(viewer_path);
}
