#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - O S   P I P E L I N E
#
#   The whole build, tree to ISO, one step after another, in a terminal you
#   can watch: image, kernel, configure, build (the base, by jhalfs), zsh,
#   runit, fetch, desktop, kernel, rekernel, esp, iso. Every step's own
#   output shows here. A step that fails stops the run and unmounts the
#   image; run this again with that step's name to carry on from it.
#
#     pipeline.zsh [STEP]
#
#   SYN_OS_WORK places the work directory (default ~/SYN-OS-build),
#   SYN_OS_PROFILE picks full or minimal. Shut down any VM that boots the
#   image first: the build mounts it.
# ------------------------------------------------------------------------------
B=${0:A:h}/build.zsh
steps=(image kernel configure build zsh runit fetch desktop kernel rekernel esp iso)
say() { print -P "\n%F{red}%B==> $(date +%H:%M) $1%b%f" }
from=${1:-image}
(( ${steps[(Ie)$from]} )) || { print "no such step: $from (one of: $steps)"; exit 1 }
[[ $from != image ]] && { say mount; zsh $B mount || exit 1 }
skip=1
for step in $steps; do
	[[ $step == $from ]] && skip=0
	(( skip )) && continue
	say $step
	zsh $B $step || {
		rc=$?
		say "STOPPED at '$step' (rc=$rc). Fix it, then: $0 $step"
		zsh $B umount 2>/dev/null
		exit $rc
	}
done
say umount; zsh $B umount
say "DONE: the image and the ISO are in ${SYN_OS_WORK:-$HOME/SYN-OS-build}"
