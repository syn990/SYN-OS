/* ------------------------------------------------------------------------
 *                            S Y N - B A R
 *
 *   Thin stub selected by flag: connects to syn-bar-core's socket,
 *   sends the matching verb, prints the reply, exits. --window-title
 *   stays connected and relays pushed lines instead (push, not poll).
 *   If syn-bar-core isn't running, prints nothing and exits quietly.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static void socket_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-bar-core.sock", runtime_dir);
}

static int connect_to_core(void) {
	char path[256];
	socket_path(path, sizeof(path));

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

static int run_request_reply(const char *verb) {
	int fd = connect_to_core();
	if (fd < 0) {
		return 0;
	}

	size_t verb_len = strlen(verb);
	if (write(fd, verb, verb_len) != (ssize_t)verb_len) {
		close(fd);
		return 1;
	}

	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, (size_t)n, stdout);
	}
	fflush(stdout);
	close(fd);
	return 0;
}

static int run_watch(const char *verb) {
	int fd = connect_to_core();
	if (fd < 0) {
		return 0;
	}

	size_t verb_len = strlen(verb);
	if (write(fd, verb, verb_len) != (ssize_t)verb_len) {
		close(fd);
		return 1;
	}

	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		fwrite(buf, 1, (size_t)n, stdout);
		fflush(stdout);
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr,
			"usage: %s <--cpu|--mem|--disk|--ssh|--vpn|--window-title|"
			"--relay-status|--relay-serving|--agent-watching|--agent-serving>\n",
			argv[0]);
		return 1;
	}

	const char *flag = argv[1];

	if (strcmp(flag, "--cpu") == 0) {
		return run_request_reply("CPU");
	}
	if (strcmp(flag, "--mem") == 0) {
		return run_request_reply("MEM");
	}
	if (strcmp(flag, "--disk") == 0) {
		return run_request_reply("DISK");
	}
	if (strcmp(flag, "--ssh") == 0) {
		return run_request_reply("SSH");
	}
	if (strcmp(flag, "--vpn") == 0) {
		return run_request_reply("VPN");
	}
	if (strcmp(flag, "--window-title") == 0) {
		return run_watch("WATCH-WINDOW-TITLE");
	}
	if (strcmp(flag, "--relay-status") == 0) {
		return run_request_reply("RELAY-STATUS");
	}
	if (strcmp(flag, "--relay-serving") == 0) {
		return run_request_reply("RELAY-SERVING");
	}
	if (strcmp(flag, "--agent-watching") == 0) {
		return run_request_reply("AGENT-WATCHING");
	}
	if (strcmp(flag, "--agent-serving") == 0) {
		return run_request_reply("AGENT-SERVING");
	}

	fprintf(stderr, "syn-bar: unknown flag '%s'\n", flag);
	return 1;
}
