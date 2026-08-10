/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-NOTIFY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_bar_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static void send_line(const char *line, size_t len) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	char path[256];
	snprintf(path, sizeof(path), "%s/syn-bar-core.sock", runtime_dir);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return;
	}
	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		write(fd, line, len);
	}
	close(fd);
}

void syn_bar_notify_meaning(const char *name) {
	char line[64];
	int n = snprintf(line, sizeof(line), "TONE-MEANING %s", name);
	if (n > 0) {
		send_line(line, (size_t)n);
	}
}

void syn_bar_notify_raw(double hz1, double hz2, double seconds) {
	char line[96];
	int n = snprintf(line, sizeof(line), "TONE-RAW %.1f %.1f %.2f", hz1, hz2, seconds);
	if (n > 0) {
		send_line(line, (size_t)n);
	}
}
