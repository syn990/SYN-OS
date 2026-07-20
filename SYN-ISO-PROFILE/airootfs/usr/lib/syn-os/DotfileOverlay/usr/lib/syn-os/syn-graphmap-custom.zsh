#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - G R A P H M A P - C U S T O M
#
#   The "Custom depth" entry of SYN-GRAPHMAP's menu: asks for a max-depth
#   via rofi (menu.xml's other two entries pass a fixed depth directly),
#   then hands off to syn-graphmap.zsh inside syn_popup::run.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-GRAPHMAP-CUSTOM (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

depth="$(syn_pick::rofi_input "Max depth (e.g. 3):" "6")"

syn_popup::run zsh /usr/lib/syn-os/syn-graphmap.zsh "" "${depth:-6}" ""
