/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_disk.h"

#include <string.h>
#include <sys/statvfs.h>

bool syn_disk_read(syn_disk_snapshot *out) {
	memset(out, 0, sizeof(*out));

	struct statvfs st;
	if (statvfs("/", &st) != 0) {
		return false;
	}

	unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
	unsigned long long used = total - (unsigned long long)st.f_bfree * st.f_frsize;

	out->total_kb = total / 1024;
	out->used_kb = used / 1024;
	return true;
}
