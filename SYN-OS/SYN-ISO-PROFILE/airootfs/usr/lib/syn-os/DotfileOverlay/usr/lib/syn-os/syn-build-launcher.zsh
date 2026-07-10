#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                     S Y N - B U I L D - L A U N C H E R
#
#   Clones syn990/SYN-OS if not already present, then runs BUILD-SYNOS-ISO.zsh.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BUILD-LAUNCHER (Tools)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-ui.zsh

REPO_DIR="$HOME/GithubProjects/SYN-OS"

[[ -d "$REPO_DIR/SYN-OS" ]] || git clone "https://github.com/syn990/SYN-OS.git" "$REPO_DIR/SYN-OS"

syn_ui::doas zsh "$REPO_DIR/SYN-OS/BUILD-SYNOS-ISO.zsh"
