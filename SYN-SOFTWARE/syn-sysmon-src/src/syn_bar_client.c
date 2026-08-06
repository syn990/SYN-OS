/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_bar_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int connect_to_core(void) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	char path[256];
	snprintf(path, sizeof(path), "%s/syn-bar-core.sock", runtime_dir);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static bool request(const char *verb, char *reply, size_t reply_size) {
	int fd = connect_to_core();
	if (fd < 0) {
		return false;
	}
	size_t verb_len = strlen(verb);
	if (write(fd, verb, verb_len) != (ssize_t)verb_len) {
		close(fd);
		return false;
	}
	ssize_t n = read(fd, reply, reply_size - 1);
	close(fd);
	if (n <= 0) {
		return false;
	}
	reply[n] = '\0';
	return true;
}

/* Same hand-rolled fixed-shape JSON pull as syn-bar-core's own
 * relay_json_number/relay_json_string — this protocol has exactly one
 * writer and one reader, both in this codebase. */
static bool json_number(const char *json, const char *key, double *out) {
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	const char *p = strstr(json, pattern);
	if (!p) {
		return false;
	}
	*out = strtod(p + strlen(pattern), NULL);
	return true;
}

static bool json_string(const char *json, const char *key, char *out, size_t out_size) {
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\": \"", key);
	const char *p = strstr(json, pattern);
	if (!p) {
		return false;
	}
	p += strlen(pattern);
	size_t i = 0;
	while (*p && *p != '"' && i < out_size - 1) {
		out[i++] = *p++;
	}
	out[i] = '\0';
	return true;
}

static syn_bar_source parse_source(const char *json) {
	char src[16] = "";
	if (!json_string(json, "source", src, sizeof(src))) {
		return SYN_BAR_SOURCE_UNAVAILABLE;
	}
	if (strcmp(src, "local") == 0) return SYN_BAR_SOURCE_LOCAL;
	if (strcmp(src, "remote") == 0) return SYN_BAR_SOURCE_REMOTE;
	if (strcmp(src, "waiting") == 0) return SYN_BAR_SOURCE_WAITING;
	return SYN_BAR_SOURCE_UNAVAILABLE;
}

bool syn_bar_client_cpu(syn_bar_cpu_reply *out) {
	memset(out, 0, sizeof(*out));
	out->source = SYN_BAR_SOURCE_UNAVAILABLE;

	char reply[2048];
	if (!request("SYSMON-CPU", reply, sizeof(reply))) {
		return false;
	}
	out->source = parse_source(reply);
	if (out->source == SYN_BAR_SOURCE_UNAVAILABLE || out->source == SYN_BAR_SOURCE_WAITING) {
		return true;
	}

	double v;
	if (json_number(reply, "total_pct", &v)) out->total_pct = v;
	json_string(reply, "host", out->host, sizeof(out->host));

	if (out->source == SYN_BAR_SOURCE_LOCAL) {
		if (json_number(reply, "load1", &v)) out->load1 = v;
		if (json_number(reply, "load5", &v)) out->load5 = v;
		if (json_number(reply, "load15", &v)) out->load15 = v;

		const char *cores = strstr(reply, "\"cores\": [");
		if (cores) {
			cores += strlen("\"cores\": [");
			int i = 0;
			char *end;
			while (i < 64) {
				double core_pct = strtod(cores, &end);
				if (end == cores) {
					break;
				}
				out->cores[i++] = core_pct;
				cores = end;
				while (*cores == ',' || *cores == ' ') {
					cores++;
				}
				if (*cores == ']') {
					break;
				}
			}
			out->core_count = i;
		}
	}
	return true;
}

bool syn_bar_client_mem(syn_bar_mem_reply *out) {
	memset(out, 0, sizeof(*out));
	out->source = SYN_BAR_SOURCE_UNAVAILABLE;

	char reply[512];
	if (!request("SYSMON-MEM", reply, sizeof(reply))) {
		return false;
	}
	out->source = parse_source(reply);
	if (out->source == SYN_BAR_SOURCE_UNAVAILABLE || out->source == SYN_BAR_SOURCE_WAITING) {
		return true;
	}

	double v;
	json_string(reply, "host", out->host, sizeof(out->host));
	if (json_number(reply, "total_kb", &v)) out->total_kb = (unsigned long long)v;

	if (out->source == SYN_BAR_SOURCE_LOCAL) {
		if (json_number(reply, "free_kb", &v)) out->free_kb = (unsigned long long)v;
		if (json_number(reply, "available_kb", &v)) out->available_kb = (unsigned long long)v;
		if (json_number(reply, "buffers_kb", &v)) out->buffers_kb = (unsigned long long)v;
		if (json_number(reply, "cached_kb", &v)) out->cached_kb = (unsigned long long)v;
		if (json_number(reply, "swap_total_kb", &v)) out->swap_total_kb = (unsigned long long)v;
		if (json_number(reply, "swap_free_kb", &v)) out->swap_free_kb = (unsigned long long)v;
	} else {
		if (json_number(reply, "used_kb", &v)) out->used_kb = (unsigned long long)v;
	}
	return true;
}
