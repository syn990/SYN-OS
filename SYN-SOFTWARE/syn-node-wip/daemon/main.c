/* ------------------------------------------------------------------------
 *   syn-noded: runs in the foreground and logs to stderr, which is what
 *   runit (and every other supervisor) wants.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (daemon)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "sn_daemon.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
	if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
		fprintf(stderr,
			"Usage: %s\n"
			"  Runs the SYN-NODE daemon in the foreground. Environment:\n"
			"  SN_HOME  state directory (default ~/.local/share/syn-node)\n"
			"  SN_PORT  TCP port (default 47900)\n"
			"  SN_SOCK  control socket (default $XDG_RUNTIME_DIR/syn-node.sock)\n",
			argv[0]);
		return 0;
	}
	return sn_daemon_run();
}
