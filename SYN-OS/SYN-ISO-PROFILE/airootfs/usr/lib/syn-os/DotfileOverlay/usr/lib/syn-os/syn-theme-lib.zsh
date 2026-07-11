# ------------------------------------------------------------------------------
#                         S Y N - T H E M E - L I B
#
#   Reads the active SYN-OS theme: which one is set, and its SYN_* palette
#   variables. Sourced only, never executed directly.
#
#   POSIX sh, not zsh — labwc's autostart has no shebang and runs under
#   /bin/sh, so this file must stay in the shell subset both sh and zsh
#   understand. No [[ ]], no typeset, no zsh-only expansions.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-THEME-LIB (Theming)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

# Prints the active theme name, defaulting to SYN-OS-RED if none is set yet.
syn_theme_current() {
  theme_ptr="$HOME/.config/syn-os/current-theme"
  if [ -f "$theme_ptr" ]; then
    cat "$theme_ptr"
  else
    printf '%s' "SYN-OS-RED"
  fi
}

# Sources the active theme's .theme file into the CALLER's shell, so its
# SYN_* variables land directly in the caller — not a subshell. A no-op if
# the file can't be found (e.g. current-theme points at a deleted theme);
# callers that need a specific variable still apply their own
# ${SYN_X:-fallback} afterward, same as before this lib existed.
syn_theme_load() {
  theme_name="$(syn_theme_current)"
  theme_file="$HOME/.config/syn-os/themes/${theme_name}.theme"
  if [ -f "$theme_file" ]; then
    . "$theme_file"
  fi
}
