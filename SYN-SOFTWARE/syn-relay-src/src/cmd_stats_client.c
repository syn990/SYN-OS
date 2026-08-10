/* ------------------------------------------------------------------------
 *   SYN-RELAY — stats client role.
 *
 *   Talks to a syn-relay running in server role on a remote machine,
 *   and drives three states (see syn_node_state.h) that waybar's
 *   cpu/memory/disk stat modules react to — ABSENT (untouched, normal
 *   local stats, the state everyone who never uses this feature stays
 *   in forever), ACTIVE (launched but not yet connected — waybar shows
 *   a dead/offline indicator), CONNECTED (waybar shows that remote
 *   machine's real live stats).
 *
 *   Logic here is relocated verbatim from the pre-merge syn-relay-client
 *   binary's main.c — see the top-level merge plan for why; the one
 *   behavior change is --connect prompting via syn-uplink-dialpad when
 *   called with no host argument (menu.xml used to shell out to a tiny
 *   wrapper script for this, now the binary does it itself).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "cmd_stats_client.h"
#include "syn_relay_conn.h"
#include "syn_relay_protocol.h"
#include "syn_node_state.h"
#include "syn_json_extract.h"
#include "syn_local_stats.h"
#include "syn_dialpad_prompt.h"
#include "syn_ssh_resolve.h"
#include "syn_bar_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/* Fire-and-forget notify-send — same fork/exec-and-forget shape as
 * cmd_launch's moonlight-qt spawn below: the caller (a waybar poll or
 * a one-off CLI command) shouldn't block on a toast the user may not
 * even see. */
static void notify(const char *title, const char *body) {
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
		}
		execlp("notify-send", "notify-send", title, body, (char *)NULL);
		_exit(127);
	}
}

/* Matches the warning/critical coloring waybar's stock cpu/memory
 * modules gave for free via their own "states" config key — that
 * feature doesn't exist for custom modules, so it's reproduced here
 * instead, only for the local (no relay) path; the relay-waiting/
 * relay-connected classes stay visually distinct on their own. */
static const char *threshold_class(double pct, double warning, double critical) {
	if (pct >= critical) {
		return "critical";
	}
	if (pct >= warning) {
		return "warning";
	}
	return "";
}

static void xml_escaped(const char *s) {
	for (const char *p = s; *p; p++) {
		switch (*p) {
		case '&': fputs("&amp;", stdout); break;
		case '<': fputs("&lt;", stdout); break;
		case '>': fputs("&gt;", stdout); break;
		case '"': fputs("&quot;", stdout); break;
		default: fputc(*p, stdout);
		}
	}
}

int cmd_activate(void) {
	if (!syn_node_state_activate()) {
		fprintf(stderr, "syn-relay: failed to save relay state\n");
		return 1;
	}
	printf("Relay client active — waiting for --connect\n");
	return 0;
}

/* Reads "os"/"capabilities" out of a STATS reply (see
 * syn_relay_protocol.h) into the state-cacheable form. Tolerant of a
 * reply predating these fields — syn_json_string/syn_json_bool leave
 * their outputs at the caller-supplied default when a key is absent, so
 * an old-format reply just yields an empty os + all-false capabilities
 * rather than failing. syn_json_bool nested under "capabilities" is
 * looked up by its bare key name — see syn_json_extract.c's contract. */
static void caps_from_stats_reply(const char *reply, syn_node_caps *caps) {
	memset(caps, 0, sizeof(*caps));
	syn_json_string(reply, "os", caps->os, sizeof(caps->os));
	syn_json_bool(reply, "apps", &caps->apps);
	syn_json_bool(reply, "watch_desktop", &caps->watch_desktop);
	syn_json_bool(reply, "watch_window", &caps->watch_window);
	syn_json_bool(reply, "host_watched", &caps->host_watched);
}

int cmd_connect(const char *host_arg) {
	char prompted[256];
	const char *ssh_target = host_arg;
	if (!ssh_target || !ssh_target[0]) {
		if (!syn_dialpad_prompt("Connect to node (IP or hostname):", prompted, sizeof(prompted))) {
			return 0; /* cancelled — not an error */
		}
		ssh_target = prompted;
	}

	/* ssh_target is what the user typed/picked (an ~/.ssh/config alias,
	 * a bare IP, a plain hostname). stats_host is what actually gets
	 * TCP-connected to for the stats protocol — resolved via `ssh -G`
	 * so an aliased Host with a HostName override (or ProxyJump) still
	 * finds the right machine; falls back to the typed string verbatim
	 * if resolution fails or there's no ssh_config entry at all. */
	char stats_host[256];
	if (!syn_ssh_resolve_hostname(ssh_target, stats_host, sizeof(stats_host))) {
		snprintf(stats_host, sizeof(stats_host), "%s", ssh_target);
	}

	char reply[SYN_RELAY_MAX_LINE];
	if (!syn_relay_request(stats_host, "STATS", reply, sizeof(reply))) {
		fprintf(stderr, "syn-relay: no syn-relay server reachable at %s\n", stats_host);
		syn_bar_notify_meaning("FAIL");
		return 1;
	}
	syn_node_caps caps;
	caps_from_stats_reply(reply, &caps);
	if (!syn_node_state_save(stats_host, ssh_target, &caps)) {
		fprintf(stderr, "syn-relay: failed to save connection state\n");
		syn_bar_notify_meaning("FAIL");
		return 1;
	}
	char body[300];
	snprintf(body, sizeof(body), "Connected to gateway %s", stats_host);
	notify("SYN-RELAY", body);
	syn_bar_notify_meaning("SUCCESS");
	printf("Connected to %s\n", stats_host);
	return 0;
}

int cmd_disconnect(void) {
	syn_node_state_clear();
	notify("SYN-RELAY", "Disconnected from gateway");
	syn_bar_notify_meaning("STOP");
	printf("Disconnected\n");
	return 0;
}

/* Common to all three --stat-* dispatchers: ABSENT means the relay
 * feature has never been touched on this machine, so the bar module
 * behaves exactly as if syn-relay didn't exist — real local numbers,
 * unconditionally, no "class" at all (the plain/default waybar look,
 * matching what cpu/memory/custom-disk always showed before this
 * feature existed). ACTIVE means launched-but-waiting: a dead/offline
 * indicator instead of a number. CONNECTED fetches the remote STATS
 * reply once and lets the caller pull whatever field it needs from it. */
static syn_node_state resolve_state(char *host_out, size_t host_out_size,
	char *remote_reply, size_t remote_reply_size, bool *remote_ok) {
	*remote_ok = false;
	syn_node_state state = syn_node_state_get(host_out, host_out_size);
	if (state == SYN_NODE_STATE_CONNECTED) {
		*remote_ok = syn_relay_request(host_out, "STATS", remote_reply, remote_reply_size);
	}
	return state;
}

int cmd_stat_cpu(void) {
	char host[256];
	char remote[SYN_RELAY_MAX_LINE];
	bool remote_ok;
	syn_node_state state = resolve_state(host, sizeof(host), remote, sizeof(remote), &remote_ok);

	if (state == SYN_NODE_STATE_ABSENT) {
		double one, five, fifteen;
		syn_local_load_avg(&one, &five, &fifteen);
		double cpu_pct = syn_local_cpu_pct();
		printf("{\"text\": \" %.0f%% \", \"tooltip\": \"Load average: %.2f %.2f %.2f\", \"class\": \"%s\"}\n",
			cpu_pct, one, five, fifteen, threshold_class(cpu_pct, 70.0, 90.0));
		return 0;
	}
	if (state == SYN_NODE_STATE_ACTIVE || !remote_ok) {
		printf("{\"text\": \" --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		return 0;
	}

	double cpu_pct = 0;
	syn_json_number(remote, "cpu_pct", &cpu_pct);
	printf("{\"text\": \" %.0f%% \", \"tooltip\": \"%s: CPU %.1f%%\", \"class\": \"relay-connected\"}\n",
		cpu_pct, host, cpu_pct);
	return 0;
}

int cmd_stat_mem(void) {
	char host[256];
	char remote[SYN_RELAY_MAX_LINE];
	bool remote_ok;
	syn_node_state state = resolve_state(host, sizeof(host), remote, sizeof(remote), &remote_ok);

	if (state == SYN_NODE_STATE_ABSENT) {
		unsigned long long used_kb, total_kb;
		syn_local_mem_kb(&used_kb, &total_kb);
		double pct = total_kb > 0 ? (double)used_kb * 100.0 / (double)total_kb : 0.0;
		printf("{\"text\": \" %.0f%% \", \"tooltip\": \"RAM: %.1fG / %.1fG\", \"class\": \"%s\"}\n",
			pct, used_kb / 1048576.0, total_kb / 1048576.0, threshold_class(pct, 75.0, 90.0));
		return 0;
	}
	if (state == SYN_NODE_STATE_ACTIVE || !remote_ok) {
		printf("{\"text\": \" --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		return 0;
	}

	double mem_used = 0, mem_total = 0;
	syn_json_number(remote, "mem_used_kb", &mem_used);
	syn_json_number(remote, "mem_total_kb", &mem_total);
	double pct = mem_total > 0 ? mem_used * 100.0 / mem_total : 0.0;
	printf("{\"text\": \" %.0f%% \", \"tooltip\": \"%s: RAM %.1fG / %.1fG\", \"class\": \"relay-connected\"}\n",
		pct, host, mem_used / 1048576.0, mem_total / 1048576.0);
	return 0;
}

int cmd_stat_disk(void) {
	char host[256];
	char remote[SYN_RELAY_MAX_LINE];
	bool remote_ok;
	syn_node_state state = resolve_state(host, sizeof(host), remote, sizeof(remote), &remote_ok);

	if (state == SYN_NODE_STATE_ABSENT) {
		unsigned long long used_kb, total_kb;
		syn_local_disk_kb(&used_kb, &total_kb);
		double pct = total_kb > 0 ? (double)used_kb * 100.0 / (double)total_kb : 0.0;
		printf("{\"text\": \"%.1fG/%.1fG\", \"tooltip\": \"/ %.1fG used of %.1fG\", \"class\": \"%s\"}\n",
			used_kb / 1048576.0, total_kb / 1048576.0,
			used_kb / 1048576.0, total_kb / 1048576.0, threshold_class(pct, 75.0, 90.0));
		return 0;
	}
	if (state == SYN_NODE_STATE_ACTIVE || !remote_ok) {
		printf("{\"text\": \"DISK --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		return 0;
	}

	double disk_used = 0, disk_total = 0;
	syn_json_number(remote, "disk_used_kb", &disk_used);
	syn_json_number(remote, "disk_total_kb", &disk_total);
	printf("{\"text\": \"%.1fG/%.1fG\", \"tooltip\": \"%s: %.1fG used of %.1fG\", \"class\": \"relay-connected\"}\n",
		disk_used / 1048576.0, disk_total / 1048576.0,
		host, disk_used / 1048576.0, disk_total / 1048576.0);
	return 0;
}

int cmd_stat_relay_status(void) {
	char host[256];
	char remote[SYN_RELAY_MAX_LINE];
	bool remote_ok;
	syn_node_state state = resolve_state(host, sizeof(host), remote, sizeof(remote), &remote_ok);

	if (state == SYN_NODE_STATE_ABSENT) {
		printf("{\"text\": \"\", \"tooltip\": \"\"}\n");
		return 0;
	}

	if (state == SYN_NODE_STATE_ACTIVE) {
		printf("{\"text\": \" no connection to remote host\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		return 0;
	}

	/* CONNECTED from here on. remote_ok false means the STATS poll that
	 * resolve_state() just did failed — the remote died or dropped off
	 * the network without anyone calling --disconnect. Render the same
	 * as ACTIVE so the bar stops claiming a connection that just failed. */
	if (!remote_ok) {
		printf("{\"text\": \" no connection to remote host\", \"tooltip\": \"Lost connection to %s\", \"class\": \"relay-waiting\"}\n", host);
		return 0;
	}

	char hostname[256] = "";
	if (!syn_json_string(remote, "hostname", hostname, sizeof(hostname)) || !hostname[0]) {
		snprintf(hostname, sizeof(hostname), "%s", host);
	}
	printf("{\"text\": \" connected: %s\", \"tooltip\": \"Connected to %s\", \"class\": \"relay-connected\"}\n",
		hostname, host);
	return 0;
}

static void print_placeholder_menu(const char *label) {
	printf("<openbox_pipe_menu>\n");
	printf("  <item label=\"");
	xml_escaped(label);
	printf("\"/>\n");
	printf("</openbox_pipe_menu>\n");
}

/* Extracts "key":"value" from p onward — one field, repeated per key by
 * syn_apps_parse_wire_reply() below. */
static void extract_field(const char *p, const char *key, char *out, size_t out_size) {
	out[0] = '\0';
	char needle[32];
	snprintf(needle, sizeof(needle), "\"%s\":\"", key);
	const char *key_pos = strstr(p, needle);
	if (!key_pos) {
		return;
	}
	const char *v = key_pos + strlen(needle);
	size_t i = 0;
	while (v[i] && v[i] != '"' && i < out_size - 1) {
		out[i] = v[i];
		i++;
	}
	out[i] = '\0';
}

int syn_apps_parse_wire_reply(const char *reply, syn_app_entry *out, int max) {
	int count = 0;
	const char *p = reply;
	while (count < max && (p = strstr(p, "\"id\":\""))) {
		char id[sizeof(out[0].id)];
		extract_field(p, "id", id, sizeof(id));
		p += 6; /* past "id":" so the next strstr in extract_field can't re-match this same id */

		if (!id[0]) {
			continue;
		}
		snprintf(out[count].id, sizeof(out[count].id), "%s", id);
		extract_field(p, "name", out[count].name, sizeof(out[count].name));
		/* icon is a theme icon NAME (e.g. "firefox"), not a path — taken
		 * straight from the remote's .desktop Icon= key. Resolution
		 * against the LOCAL icon theme is labwc's job; a name absent
		 * from the local theme just renders with no icon, which is a
		 * cosmetic degradation, not a bug to work around here. */
		extract_field(p, "icon", out[count].icon, sizeof(out[count].icon));
		extract_field(p, "exec", out[count].exec, sizeof(out[count].exec));
		count++;
	}
	return count;
}

int cmd_list_apps(const char *host_override) {
	char host[256];
	if (host_override && host_override[0]) {
		snprintf(host, sizeof(host), "%s", host_override);
	} else if (syn_node_state_get(host, sizeof(host)) != SYN_NODE_STATE_CONNECTED) {
		print_placeholder_menu("Not connected to a node");
		return 0;
	}

	char reply[8192];
	if (!syn_relay_request(host, "LIST_APPS", reply, sizeof(reply))) {
		print_placeholder_menu("Node unreachable");
		return 0;
	}

	static syn_app_entry apps[SYN_APPS_MAX];
	int count = syn_apps_parse_wire_reply(reply, apps, SYN_APPS_MAX);

	int streaming = host_override && host_override[0];

	printf("<openbox_pipe_menu>\n");
	int found = 0;
	for (int i = 0; i < count; i++) {
		if (!apps[i].name[0]) {
			continue;
		}
		printf("  <item label=\"");
		xml_escaped(apps[i].name);
		if (apps[i].icon[0]) {
			printf("\" icon=\"");
			xml_escaped(apps[i].icon);
		}
		if (streaming) {
			printf("\">\n    <action name=\"Execute\"><command>foot -e /usr/lib/syn-os/syn-relay --stream-app &quot;");
			xml_escaped(apps[i].id);
			printf("&quot; &quot;");
			xml_escaped(host);
			printf("&quot;</command></action>\n  </item>\n");
		} else {
			printf("\">\n    <action name=\"Execute\"><command>syn-relay --launch &quot;");
			xml_escaped(apps[i].id);
			printf("&quot;</command></action>\n  </item>\n");
		}
		found++;
	}
	if (!found) {
		printf("  <item label=\"No applications found\"/>\n");
	}
	printf("</openbox_pipe_menu>\n");
	return 0;
}

int cmd_launch(const char *id) {
	char host[256];
	if (syn_node_state_get(host, sizeof(host)) != SYN_NODE_STATE_CONNECTED) {
		fprintf(stderr, "syn-relay: not connected to a node\n");
		syn_bar_notify_meaning("FAIL");
		return 1;
	}

	char cmd[SYN_RELAY_MAX_LINE];
	snprintf(cmd, sizeof(cmd), "LAUNCH_APP %s", id);

	char reply[SYN_RELAY_MAX_LINE];
	if (!syn_relay_request(host, cmd, reply, sizeof(reply))) {
		fprintf(stderr, "syn-relay: launch request failed (node unreachable)\n");
		syn_bar_notify_meaning("FAIL");
		return 1;
	}

	bool ok = false;
	syn_json_bool(reply, "ok", &ok);
	if (!ok) {
		fprintf(stderr, "syn-relay: launch failed: %s\n", reply);
		syn_bar_notify_meaning("FAIL");
		return 1;
	}

	syn_bar_notify_meaning("SUCCESS");
	printf("Launched\n");
	return 0;
}
