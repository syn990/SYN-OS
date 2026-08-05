/* ------------------------------------------------------------------------
 *   NVIDIA GPU load via nvidia-smi(1). No existing GPU-stat code exists
 *   anywhere else in SYN-OS to port from — AMD/Intel /sys/class/drm
 *   reading is out of scope for v1 (NVIDIA-only, matches the actual
 *   gaming-rig use case this was built for).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_GPU_H
#define SYN_GPU_H

#include <stdbool.h>

/* Runs `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits`
 * via popen and parses the first line as a percentage into *out_pct.
 * Returns false (leaving *out_pct at 0.0) if nvidia-smi isn't installed,
 * fails to run, or its output can't be parsed — never blocks or hangs
 * the caller waiting on a GPU that doesn't exist. */
bool syn_gpu_read(double *out_pct);

#endif
