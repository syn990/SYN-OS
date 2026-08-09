/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_dialpad_prompt.h"

#include <stdio.h>
#include <string.h>

bool syn_dialpad_prompt(const char *prompt, char *out, size_t out_size) {
	(void)prompt;
	if (out_size > 0) {
		out[0] = '\0';
	}

	FILE *pipe = popen("/usr/lib/syn-os/syn-uplink-dialpad", "r");
	if (!pipe) {
		return false;
	}

	bool got_line = fgets(out, (int)out_size, pipe) != NULL;
	pclose(pipe);

	if (!got_line) {
		return false;
	}
	size_t len = strlen(out);
	while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
		out[--len] = '\0';
	}
	return len > 0;
}
