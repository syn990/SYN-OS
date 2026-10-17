/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_hum_send.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static int connect_core(void) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/syn-bar-core.sock", runtime_dir ? runtime_dir : "/tmp");
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	/* syn-bar-core answers within its own 200 ms or not at all */
	struct timeval tv = {.tv_sec = 0, .tv_usec = 500000};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	return fd;
}

int syn_hum_send(const char *line) {
	int fd = connect_core();
	if (fd < 0) return -1;
	ssize_t len = (ssize_t)strlen(line);
	int ok = write(fd, line, (size_t)len) == len;
	close(fd);
	return ok ? 0 : -1;
}

int syn_hum_ask(const char *line, char *reply, size_t len) {
	int fd = connect_core();
	if (fd < 0) return -1;
	ssize_t want = (ssize_t)strlen(line);
	if (write(fd, line, (size_t)want) != want) {
		close(fd);
		return -1;
	}
	size_t got = 0;
	ssize_t n;
	while (got + 1 < len && (n = read(fd, reply + got, len - 1 - got)) > 0) {
		got += (size_t)n;
	}
	reply[got] = '\0';
	close(fd);
	return (int)got;
}
