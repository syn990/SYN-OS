#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - G R A P H M A P - F U L L
#
#   The "Full (deep scan)" entry of SYN-GRAPHMAP's menu — depth 999
#   (effectively unlimited), directory/format still prompted by
#   syn-graphmap.zsh itself, run inside syn_popup::run so the window
#   closes itself when it's done.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-GRAPHMAP-FULL (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_popup::run zsh /usr/lib/syn-os/syn-graphmap.zsh "" 999 ""
