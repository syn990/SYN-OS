/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_gpu.h"

#include <stdio.h>

bool syn_gpu_read(double *out_pct) {
	*out_pct = 0.0;

	FILE *p = popen("nvidia-smi --query-gpu=utilization.gpu "
	                 "--format=csv,noheader,nounits 2>/dev/null", "r");
	if (!p) {
		return false;
	}

	char line[64];
	bool ok = false;
	if (fgets(line, sizeof(line), p)) {
		double v;
		if (sscanf(line, "%lf", &v) == 1) {
			*out_pct = v;
			ok = true;
		}
	}
	pclose(p);
	return ok;
}
