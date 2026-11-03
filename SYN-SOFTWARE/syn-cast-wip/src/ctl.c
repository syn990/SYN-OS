/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "ctl.h"
#include "app.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static struct {
	int fd;
	char buf[2048];
	size_t len;
	int64_t since;
} clients[CTL_MAX_CLIENTS] = {[0 ... CTL_MAX_CLIENTS - 1] = {.fd = -1}};

const char *ctl_path(char *out, size_t cap) {
	const char *env = getenv("SYN_CAST_SOCK");
	if (env && *env) {
		snprintf(out, cap, "%s", env);
	} else {
		const char *rt = getenv("XDG_RUNTIME_DIR");
		snprintf(out, cap, "%s/syn-cast.sock", rt && *rt ? rt : "/tmp");
	}
	return out;
}

static bool fill_addr(struct sockaddr_un *a, const char *path) {
	if (strlen(path) >= sizeof a->sun_path) {
		LOG("control socket path too long: %s", path);
		return false;
	}
	str_copy(a->sun_path, sizeof a->sun_path, path);
	return true;
}

static int connect_to(const char *path) {
	struct sockaddr_un a = {.sun_family = AF_UNIX};
	if (!fill_addr(&a, path)) {
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof a) == 0) {
		return fd;
	}
	if (fd >= 0) {
		close(fd);
	}
	return -1;
}

int ctl_listen(void) {
	char path[256];
	ctl_path(path, sizeof path);
	int other = connect_to(path);
	if (other >= 0) {
		close(other);
		return -2;
	}
	unlink(path); /* a stale socket from a crash */
	struct sockaddr_un a = {.sun_family = AF_UNIX};
	if (!fill_addr(&a, path)) {
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 8) != 0) {
		LOG("control socket %s: %s", path, strerror(errno));
		if (fd >= 0) {
			close(fd);
		}
		return -1;
	}
	chmod(path, 0600);
	return fd;
}

void ctl_close(int fd) {
	if (fd < 0) {
		return;
	}
	char path[256];
	close(fd);
	unlink(ctl_path(path, sizeof path));
	for (int i = 0; i < CTL_MAX_CLIENTS; i++) {
		if (clients[i].fd >= 0) {
			close(clients[i].fd);
			clients[i].fd = -1;
		}
	}
}

int ctl_fds(int listen_fd, int *fds, int max) {
	int n = 0;
	if (listen_fd >= 0 && n < max) {
		fds[n++] = listen_fd;
	}
	for (int i = 0; i < CTL_MAX_CLIENTS && n < max; i++) {
		if (clients[i].fd >= 0) {
			fds[n++] = clients[i].fd;
		}
	}
	return n;
}

void ctl_on_listen(int listen_fd) {
	for (;;) {
		int fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
		if (fd < 0) {
			return;
		}
		int slot = -1;
		for (int i = 0; i < CTL_MAX_CLIENTS; i++) {
			if (clients[i].fd < 0) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			close(fd);
			continue;
		}
		clients[slot].fd = fd;
		clients[slot].len = 0;
		clients[slot].since = now_ms();
	}
}

static void reply(int fd, const char *s) {
	size_t n = strlen(s);
	while (n) {
		ssize_t w = write(fd, s, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN) {
				struct pollfd p = {.fd = fd, .events = POLLOUT};
				if (poll(&p, 1, 1000) == 1) continue;
			}
			return;
		}
		s += w;
		n -= (size_t)w;
	}
}

static bool is(const char *line, const char *word, const char **arg) {
	size_t n = strlen(word);
	if (strncasecmp(line, word, n) != 0 || (line[n] && line[n] != ' ')) {
		return false;
	}
	*arg = line + n;
	while (**arg == ' ') {
		(*arg)++;
	}
	return true;
}

static void run(int fd, const char *line) {
	char err[300] = "";
	const char *arg;
	int rc = 0;
	if (is(line, "PTT", &arg)) {
		if (strcasecmp(arg, "DOWN") == 0) app_ptt_down();
		else if (strcasecmp(arg, "UP") == 0) app_ptt_up();
		else if (strcasecmp(arg, "TOGGLE") == 0) app_ptt_toggle();
		else {
			snprintf(err, sizeof err, "PTT DOWN, UP or TOGGLE");
			rc = -1;
		}
	} else if (is(line, "SAY", &arg)) {
		rc = app_say_file(arg, err, sizeof err);
	} else if (is(line, "TEXT", &arg)) {
		rc = app_send_text(arg, err, sizeof err);
	} else if (is(line, "IMAGE", &arg)) {
		rc = app_send_image(arg, err, sizeof err);
	} else if (is(line, "LOC", &arg)) {
		double lat, lon;
		int used = 0;
		if (sscanf(arg, "%lf %lf %n", &lat, &lon, &used) < 2) {
			snprintf(err, sizeof err, "LOC LAT LON [ADDRESS]");
			rc = -1;
		} else {
			rc = app_send_location(lat, lon, arg + used, err, sizeof err);
		}
	} else if (is(line, "CHANNEL", &arg)) {
		int k = app_find_channel(arg);
		if (k < 0) {
			snprintf(err, sizeof err, "no channel %s in the config", arg);
			rc = -1;
		} else {
			app_select_channel(k);
		}
	} else if (is(line, "REPLAY", &arg)) {
		app_replay_last();
	} else if (is(line, "STOP", &arg)) {
		app_stop_playback();
	} else if (is(line, "ROOM", &arg)) {
		app_toggle_room();
	} else if (is(line, "VOX", &arg)) {
		app_toggle_vox();
	} else if (is(line, "OFFLINE", &arg)) {
		app_set_offline(true);
	} else if (is(line, "ONLINE", &arg)) {
		app_set_offline(false);
	} else if (is(line, "SPEED", &arg)) {
		app_cycle_speed();
	} else if (is(line, "STATUS", &arg)) {
		char *s = app_status_json();
		reply(fd, s);
		reply(fd, "\n");
		free(s);
		return;
	} else if (is(line, "SNAPSHOT", &arg)) {
		if (arg[0] != '/') {
			snprintf(err, sizeof err, "SNAPSHOT /absolute/path.ppm");
			rc = -1;
		} else {
			str_copy(app.snapshot, sizeof app.snapshot, arg);
			app.dirty = true;
		}
	} else if (is(line, "QUIT", &arg)) {
		app.quit = true;
	} else {
		snprintf(err, sizeof err, "unknown command (see syn-cast --help)");
		rc = -1;
	}
	if (rc == 0) {
		reply(fd, "ok\n");
	} else {
		char out[400];
		snprintf(out, sizeof out, "error: %s\n", err);
		reply(fd, out);
	}
}

void ctl_on_client(int fd) {
	for (int i = 0; i < CTL_MAX_CLIENTS; i++) {
		if (clients[i].fd != fd) {
			continue;
		}
		ssize_t r = read(fd, clients[i].buf + clients[i].len, sizeof clients[i].buf - 1 - clients[i].len);
		if (r < 0 && errno == EAGAIN) {
			return;
		}
		bool done = r <= 0;
		if (r > 0) {
			clients[i].len += (size_t)r;
			clients[i].buf[clients[i].len] = 0;
			char *nl = strchr(clients[i].buf, '\n');
			if (nl || clients[i].len == sizeof clients[i].buf - 1) {
				if (nl) *nl = 0;
				run(fd, clients[i].buf);
				done = true;
			}
		}
		if (done) {
			close(fd);
			clients[i].fd = -1;
		}
		return;
	}
}

int ctl_call(const char *line) {
	char path[256];
	int fd = connect_to(ctl_path(path, sizeof path));
	if (fd < 0) {
		fprintf(stderr, "syn-cast is not running (no %s)\n", path);
		return 2;
	}
	char msg[2100];
	snprintf(msg, sizeof msg, "%s\n", line);
	if (write(fd, msg, strlen(msg)) < 0) {
		close(fd);
		return 2;
	}
	shutdown(fd, SHUT_WR);
	char buf[4096];
	ssize_t r;
	int rc = 0;
	bool first = true;
	while ((r = read(fd, buf, sizeof buf)) > 0) {
		if (first && strncmp(buf, "error:", 6) == 0) {
			rc = 1;
		}
		first = false;
		fwrite(buf, 1, (size_t)r, stdout);
	}
	close(fd);
	return rc;
}
