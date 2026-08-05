/* ------------------------------------------------------------------------
 *   SYN-RELAY — stats server role.
 *
 *   Small unauthenticated TCP daemon: reports this machine's live
 *   stats, lists its installed applications, and launches them on
 *   request. Meant to run on a machine the user owns/administers so a
 *   SYN-OS desktop elsewhere can connect to it (stats client role,
 *   cmd_stats_client.c) — the bar shows this machine's real CPU/RAM/disk
 *   instead of the local host's, and the right-click app menu shows
 *   this machine's real installed software.
 *
 *   No authentication, no encryption. Deliberate for v1 — trusted LAN
 *   only. LAUNCH_APP runs arbitrary installed software on request, so
 *   never bind this beyond a private network.
 *
 *   Logic here is relocated verbatim from the pre-merge syn-relay-server
 *   binary's linux/main.c — see the top-level merge plan for why.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats server role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include "cmd_stats_server.h"
#include "syn_relay_protocol.h"
#include "syn_stats.h"
#include "syn_disk.h"
#include "syn_gpu.h"
#include "syn_sunshine.h"
#include "syn_apps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

/* Set by the SIGTERM handler installed in run_server(); the accept loop
 * checks this whenever accept() returns -1 (which it now will, via
 * EINTR, once a caught-not-ignored signal arrives) so --stop can request
 * a clean shutdown instead of the default disposition's immediate,
 * no-cleanup termination. */
static volatile sig_atomic_t g_stop_requested = 0;

static void handle_sigterm(int sig) {
	(void)sig;
	g_stop_requested = 1;
}

static void pid_file_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-relay.server.pid", runtime_dir);
}

static void last_client_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-relay.server.last-client", runtime_dir);
}

/* Reads the PID file and checks whether that process is actually still
 * alive (kill(pid, 0) — sends no signal, just tests existence/permission)
 * rather than trusting a stale file left behind by a crash. Returns 0 if
 * not running (no file, unreadable, or the pid is dead). */
static pid_t running_server_pid(void) {
	char path[512];
	pid_file_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	long pid_val = 0;
	int ok = fscanf(f, "%ld", &pid_val) == 1;
	fclose(f);
	if (!ok || pid_val <= 0) {
		return 0;
	}

	pid_t pid = (pid_t)pid_val;
	if (kill(pid, 0) != 0) {
		return 0; /* stale file — process is gone */
	}
	return pid;
}

static void print_json_escaped(FILE *out, const char *s) {
	for (const char *p = s; *p; p++) {
		switch (*p) {
		case '"': fputs("\\\"", out); break;
		case '\\': fputs("\\\\", out); break;
		case '\n': fputs("\\n", out); break;
		default: fputc(*p, out);
		}
	}
}

static void handle_stats(FILE *out) {
	syn_cpu_snapshot prev, cur;
	syn_stats_cpu_read(&prev);
	usleep(150000); /* 150ms floor for a meaningful tick delta, same
	                  * rationale as syn-sysmon's TUI redraw comment */
	syn_stats_cpu_read(&cur);
	double cpu_pct = syn_stats_cpu_usage(&prev, &cur, -1);

	syn_mem_snapshot mem;
	syn_stats_mem_read(&mem);

	syn_disk_snapshot disk;
	syn_disk_read(&disk);

	double gpu_pct;
	bool have_gpu = syn_gpu_read(&gpu_pct);

	bool sunshine_running = syn_sunshine_is_running();

	char hostname[256] = "unknown";
	gethostname(hostname, sizeof(hostname));

	fprintf(out, "{\"hostname\":\"");
	print_json_escaped(out, hostname);
	fprintf(out, "\",\"cpu_pct\":%.1f,\"mem_used_kb\":%llu,\"mem_total_kb\":%llu,"
	             "\"disk_used_kb\":%llu,\"disk_total_kb\":%llu,\"gpu_pct\":",
		cpu_pct,
		mem.total_kb - mem.available_kb, mem.total_kb,
		disk.used_kb, disk.total_kb);
	if (have_gpu) {
		fprintf(out, "%.1f", gpu_pct);
	} else {
		fprintf(out, "null");
	}
	fprintf(out, ",\"sunshine\":{\"running\":%s,\"port\":%d}}\n",
		sunshine_running ? "true" : "false", SYN_SUNSHINE_PORT);
}

static void handle_list_apps(FILE *out) {
	static syn_app_entry apps[SYN_APPS_MAX];
	int count = syn_apps_list(apps, SYN_APPS_MAX);

	fprintf(out, "{\"apps\":[");
	for (int i = 0; i < count; i++) {
		if (i > 0) fprintf(out, ",");
		fprintf(out, "{\"id\":\"");
		print_json_escaped(out, apps[i].id);
		fprintf(out, "\",\"name\":\"");
		print_json_escaped(out, apps[i].name);
		fprintf(out, "\",\"icon\":\"");
		print_json_escaped(out, apps[i].icon);
		fprintf(out, "\"}");
	}
	fprintf(out, "]}\n");
}

static void handle_launch_app(FILE *out, const char *id) {
	if (id[0] == '\0') {
		fprintf(out, "{\"ok\":false,\"error\":\"missing id\"}\n");
		return;
	}
	if (syn_apps_launch(id)) {
		fprintf(out, "{\"ok\":true}\n");
	} else {
		fprintf(out, "{\"ok\":false,\"error\":\"launch failed\"}\n");
	}
}

/* Fire-and-forget notify-send — exact fork+execlp+_exit(127) idiom used
 * throughout this codebase (no waitpid: the accept loop shouldn't block
 * on a notification the user may not even see). */
static void notify(const char *title, const char *body) {
	pid_t pid = fork();
	if (pid == 0) {
		execlp("notify-send", "notify-send", title, body, (char *)NULL);
		_exit(127);
	}
}

/* Called from the accept-loop parent (not the forked-per-connection
 * child) right after a client's IP is known. One syn-relay server-role
 * process serves many short-lived connections — STATS gets polled every
 * few seconds by anything connected — so naively notifying on every
 * accept() would spam a toast every poll interval. Only notify when the
 * connecting IP differs from last-client's existing content, or more
 * than 60s have passed since that file's mtime for the same IP: "a new
 * client showed up" or "this client came back after being gone a
 * while", not "this client polled again". */
static void note_client_connected(const char *ip) {
	char path[512];
	last_client_path(path, sizeof(path));

	char prev_ip[256] = "";
	time_t prev_mtime = 0;
	FILE *f = fopen(path, "r");
	if (f) {
		if (fgets(prev_ip, sizeof(prev_ip), f)) {
			size_t len = strlen(prev_ip);
			while (len > 0 && (prev_ip[len - 1] == '\n' || prev_ip[len - 1] == '\r')) {
				prev_ip[--len] = '\0';
			}
		}
		fclose(f);
		struct stat st;
		if (stat(path, &st) == 0) {
			prev_mtime = st.st_mtime;
		}
	}

	bool is_new_or_stale = (strcmp(prev_ip, ip) != 0) || (time(NULL) - prev_mtime > 60);

	f = fopen(path, "w");
	if (f) {
		fprintf(f, "%s\n", ip);
		fclose(f);
	}

	if (is_new_or_stale) {
		char body[300];
		snprintf(body, sizeof(body), "%s connected to this machine", ip);
		notify("SYN-RELAY", body);
	}
}

static void handle_client(int fd) {
	FILE *in = fdopen(fd, "r");
	FILE *out = fdopen(dup(fd), "w");
	if (!in || !out) {
		close(fd);
		return;
	}

	char line[SYN_RELAY_MAX_LINE];
	if (fgets(line, sizeof(line), in)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}

		if (strcmp(line, "STATS") == 0) {
			handle_stats(out);
		} else if (strcmp(line, "LIST_APPS") == 0) {
			handle_list_apps(out);
		} else if (strncmp(line, "LAUNCH_APP ", 11) == 0) {
			handle_launch_app(out, line + 11);
		} else {
			fprintf(out, "{\"ok\":false,\"error\":\"unknown command\"}\n");
		}
	}

	fclose(out);
	fclose(in);
}

/* Creates, binds, and listens on the relay socket. Deliberately run in
 * cmd_server_start()'s parent, BEFORE forking — a bind() failure (e.g.
 * another instance already holds the port, orphaned or otherwise) needs
 * to be reported synchronously to whoever ran --start, not discovered
 * only by reading a log file later. The returned fd is inherited by the
 * daemonized child unchanged. Returns -1 on failure (socket/bind/listen
 * already perror()'d). */
static int open_listen_socket(void) {
	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("syn-relay: socket");
		return -1;
	}

	int yes = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SYN_RELAY_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("syn-relay: bind");
		close(listen_fd);
		return -1;
	}
	if (listen(listen_fd, 8) != 0) {
		perror("syn-relay: listen");
		close(listen_fd);
		return -1;
	}
	return listen_fd;
}

/* The accept loop itself — runs after cmd_server_start() has already
 * bound the socket (in the parent, see open_listen_socket()) and
 * daemonized. Writes its own PID to the pid file right before the loop
 * starts, so a racing --status/--stop always sees either "no file" or
 * "a live, correct pid" — never a half-written one. */
static int run_server(int listen_fd) {
	signal(SIGCHLD, SIG_IGN); /* reap forked LAUNCH_APP children automatically */
	signal(SIGTERM, handle_sigterm);

	char pid_path[512];
	pid_file_path(pid_path, sizeof(pid_path));
	FILE *pf = fopen(pid_path, "w");
	if (pf) {
		fprintf(pf, "%d\n", getpid());
		fclose(pf);
	}

	fprintf(stderr, "syn-relay: serving on :%d (unauthenticated, LAN-only)\n", SYN_RELAY_PORT);

	while (!g_stop_requested) {
		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
		if (client_fd < 0) {
			continue; /* EINTR from SIGTERM re-checks g_stop_requested above */
		}

		note_client_connected(inet_ntoa(client_addr.sin_addr));

		pid_t pid = fork();
		if (pid == 0) {
			close(listen_fd);
			handle_client(client_fd);
			_exit(0);
		}
		close(client_fd);
	}

	close(listen_fd);
	remove(pid_path);
	return 0;
}

int cmd_server_start(void) {
	pid_t existing = running_server_pid();
	if (existing != 0) {
		fprintf(stderr, "syn-relay: server role already running (pid %d)\n", existing);
		return 1;
	}

	/* Bind BEFORE forking, in this (still-foreground) process — a
	 * bind() failure (stale/orphaned instance still holding the port,
	 * permission issue, etc.) is reported synchronously to whoever ran
	 * --start, instead of only being discoverable later by reading the
	 * log file while --status wrongly claims "not running" because the
	 * forked child died before ever writing its PID file. */
	int listen_fd = open_listen_socket();
	if (listen_fd < 0) {
		return 1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("syn-relay: fork");
		close(listen_fd);
		return 1;
	}
	if (pid > 0) {
		/* Parent: the socket is bound and listening, so report success
		 * and return immediately — the menu.xml caller shouldn't block
		 * on a daemon that runs forever. */
		close(listen_fd);
		return 0;
	}

	/* Child: detach fully before entering the accept loop. stdin goes to
	 * /dev/null; stdout/stderr go to a small log file rather than
	 * /dev/null too, so any later runtime errors are recoverable after
	 * the fact instead of vanishing — there's no terminal to see them on
	 * when launched from menu.xml's Execute action. */
	setsid();
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
	}
	char log_path[512];
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	snprintf(log_path, sizeof(log_path), "%s/syn-relay.server.log", runtime_dir ? runtime_dir : "/tmp");
	int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log_fd >= 0) {
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
	}
	return run_server(listen_fd);
}

int cmd_server_stop(void) {
	pid_t pid = running_server_pid();
	if (pid == 0) {
		fprintf(stderr, "syn-relay: server role not running\n");
		return 1;
	}
	if (kill(pid, SIGTERM) != 0) {
		perror("syn-relay: kill");
		return 1;
	}
	printf("Stopped (pid %d)\n", pid);
	return 0;
}

int cmd_server_status(void) {
	pid_t pid = running_server_pid();
	if (pid == 0) {
		printf("not running\n");
		return 0;
	}
	printf("running (pid %d)\n", pid);
	return 0;
}

int cmd_stat_serving(void) {
	if (running_server_pid() == 0) {
		printf("{\"text\": \"\", \"tooltip\": \"\"}\n");
		return 0;
	}

	char path[512];
	last_client_path(path, sizeof(path));

	char ip[256] = "";
	time_t mtime = 0;
	FILE *f = fopen(path, "r");
	if (f) {
		if (fgets(ip, sizeof(ip), f)) {
			size_t len = strlen(ip);
			while (len > 0 && (ip[len - 1] == '\n' || ip[len - 1] == '\r')) {
				ip[--len] = '\0';
			}
		}
		fclose(f);
		struct stat st;
		if (stat(path, &st) == 0) {
			mtime = st.st_mtime;
		}
	}

	bool fresh = ip[0] && (time(NULL) - mtime <= 30);
	if (fresh) {
		printf("{\"text\": \" serving: %s\", \"tooltip\": \"%s connected\", \"class\": \"relay-connected\"}\n", ip, ip);
	} else {
		printf("{\"text\": \" serving (idle)\", \"tooltip\": \"syn-relay server role running, no client connected\", \"class\": \"relay-waiting\"}\n");
	}
	return 0;
}
