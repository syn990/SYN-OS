#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - P I P E - S U P E R F I L E
#
#   Labwc menu entry for superfile: opens it in a foot terminal.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-SUPERFILE (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

exec /usr/bin/foot -e /usr/bin/spf
