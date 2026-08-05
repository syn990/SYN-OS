/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_sunshine.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static bool process_named_sunshine_exists(void) {
	DIR *proc = opendir("/proc");
	if (!proc) {
		return false;
	}

	bool found = false;
	struct dirent *ent;
	while (!found && (ent = readdir(proc))) {
		if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
			continue;
		}
		char comm_path[64];
		snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", ent->d_name);
		FILE *f = fopen(comm_path, "r");
		if (!f) {
			continue;
		}
		char comm[32] = {0};
		if (fgets(comm, sizeof(comm), f)) {
			size_t len = strlen(comm);
			while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r')) {
				comm[--len] = '\0';
			}
			if (strcmp(comm, "sunshine") == 0) {
				found = true;
			}
		}
		fclose(f);
	}
	closedir(proc);
	return found;
}

static bool port_is_listening(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return false;
	}

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	bool connected = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
	close(fd);
	return connected;
}

bool syn_sunshine_is_running(void) {
	return process_named_sunshine_exists() && port_is_listening(SYN_SUNSHINE_PORT);
}
