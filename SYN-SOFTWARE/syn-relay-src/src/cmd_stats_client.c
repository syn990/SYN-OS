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
 *   behavior change is --connect prompting via rofi when called with no
 *   host argument (menu.xml used to shell out to a tiny wrapper script
 *   for this, now the binary does it itself).
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
#include "syn_rofi_prompt.h"

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

int cmd_connect(const char *host_arg) {
	char prompted[256];
	const char *host = host_arg;
	if (!host || !host[0]) {
		if (!syn_rofi_prompt("Connect to node (IP or hostname):", prompted, sizeof(prompted))) {
			return 0; /* cancelled — not an error */
		}
		host = prompted;
	}

	char reply[SYN_RELAY_MAX_LINE];
	if (!syn_relay_request(host, "STATS", reply, sizeof(reply))) {
		fprintf(stderr, "syn-relay: no syn-relay server reachable at %s\n", host);
		return 1;
	}
	if (!syn_node_state_save(host)) {
		fprintf(stderr, "syn-relay: failed to save connection state\n");
		return 1;
	}
	char body[300];
	snprintf(body, sizeof(body), "Connected to gateway %s", host);
	notify("SYN-RELAY", body);
	printf("Connected to %s\n", host);
	return 0;
}

int cmd_disconnect(void) {
	syn_node_state_clear();
	notify("SYN-RELAY", "Disconnected from gateway");
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

int cmd_list_apps(void) {
	char host[256];
	if (syn_node_state_get(host, sizeof(host)) != SYN_NODE_STATE_CONNECTED) {
		print_placeholder_menu("Not connected to a node");
		return 0;
	}

	char reply[8192];
	if (!syn_relay_request(host, "LIST_APPS", reply, sizeof(reply))) {
		print_placeholder_menu("Node unreachable");
		return 0;
	}

	printf("<openbox_pipe_menu>\n");
	/* Walk each {"id":"...","name":"...","icon":"..."} object by hand —
	 * same "no JSON library, this repo's own fixed shape" rationale as
	 * syn_json_extract.h. */
	const char *p = reply;
	int found = 0;
	while ((p = strstr(p, "\"id\":\""))) {
		p += 6;
		char id[512];
		size_t i = 0;
		while (*p && *p != '"' && i < sizeof(id) - 1) {
			id[i++] = *p++;
		}
		id[i] = '\0';

		char name[256] = "";
		const char *name_key = strstr(p, "\"name\":\"");
		if (name_key) {
			name_key += 8;
			i = 0;
			while (*name_key && *name_key != '"' && i < sizeof(name) - 1) {
				name[i++] = *name_key++;
			}
			name[i] = '\0';
		}

		/* icon is a theme icon NAME (e.g. "firefox"), not a path — taken
		 * straight from the remote's .desktop Icon= key. Resolution
		 * against the LOCAL icon theme is labwc's job; a name absent
		 * from the local theme just renders with no icon, which is a
		 * cosmetic degradation, not a bug to work around here. */
		char icon[256] = "";
		const char *icon_key = strstr(p, "\"icon\":\"");
		if (icon_key) {
			icon_key += 8;
			i = 0;
			while (*icon_key && *icon_key != '"' && i < sizeof(icon) - 1) {
				icon[i++] = *icon_key++;
			}
			icon[i] = '\0';
		}

		if (name[0]) {
			printf("  <item label=\"");
			xml_escaped(name);
			if (icon[0]) {
				printf("\" icon=\"");
				xml_escaped(icon);
			}
			printf("\">\n    <action name=\"Execute\"><command>syn-relay --launch &quot;");
			xml_escaped(id);
			printf("&quot;</command></action>\n  </item>\n");
			found++;
		}
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
		return 1;
	}

	char cmd[SYN_RELAY_MAX_LINE];
	snprintf(cmd, sizeof(cmd), "LAUNCH_APP %s", id);

	char reply[SYN_RELAY_MAX_LINE];
	if (!syn_relay_request(host, cmd, reply, sizeof(reply))) {
		fprintf(stderr, "syn-relay: launch request failed (node unreachable)\n");
		return 1;
	}

	bool ok = false;
	syn_json_bool(reply, "ok", &ok);
	if (!ok) {
		fprintf(stderr, "syn-relay: launch failed: %s\n", reply);
		return 1;
	}

	printf("Launched\n");
	return 0;
}
