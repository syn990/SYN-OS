# =============================================================================
#                                 SYN-OS .zshrc
#             Official Zsh Configuration for the SYN-OS Environment
# -----------------------------------------------------------------------------
#   Zsh setup for SYN-OS. Provides completions, autosuggestions,
#   syntax highlighting, git integration, custom prompt, and useful defaults.
#   Designed to be fast, clean, and self-contained.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================


# -----------------------------------------------------------------------------
# Environment
# -----------------------------------------------------------------------------
export LANG=en_GB.UTF-8
export EDITOR='nano'

# XDG paths (cleaner filesystem layout)
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-$HOME/.cache}"
export XDG_STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"

# -----------------------------------------------------------------------------
# History (Persistent + Shared Across Sessions)
# -----------------------------------------------------------------------------
mkdir -p "$XDG_STATE_HOME/zsh"

HISTFILE="$XDG_STATE_HOME/zsh/history"
HISTSIZE=20000
SAVEHIST=20000

setopt appendhistory
setopt sharehistory
setopt inc_append_history
setopt hist_ignore_dups
setopt hist_ignore_space
setopt hist_reduce_blanks
setopt hist_verify

# -----------------------------------------------------------------------------
# Completion System (fast + cached)
# -----------------------------------------------------------------------------
autoload -Uz compinit bashcompinit
zmodload -i zsh/complist

if [[ ! -f "$XDG_CACHE_HOME/zsh/zcompdump" ]]; then
  mkdir -p "$XDG_CACHE_HOME/zsh"
fi

compinit -d "$XDG_CACHE_HOME/zsh/zcompdump"

setopt auto_menu
setopt complete_in_word

# -----------------------------------------------------------------------------
# Keybindings
# -----------------------------------------------------------------------------
bindkey -e
bindkey '^H' backward-kill-word
bindkey '^[[3;5~' kill-word

# Better history search
bindkey '^R' history-incremental-search-backward
bindkey '^S' history-incremental-search-forward

# -----------------------------------------------------------------------------
# Shell Behaviour
# -----------------------------------------------------------------------------
setopt prompt_subst
setopt nocaseglob
setopt interactivecomments
unsetopt beep

# -----------------------------------------------------------------------------
# Optional Tool Integrations
# -----------------------------------------------------------------------------
# fzf
if command -v fzf >/dev/null 2>&1; then
  [[ -r /usr/share/fzf/completion.zsh ]] && source /usr/share/fzf/completion.zsh
  [[ -r /usr/share/fzf/key-bindings.zsh ]] && source /usr/share/fzf/key-bindings.zsh
fi

# zoxide
if command -v zoxide >/dev/null 2>&1; then
  eval "$(zoxide init zsh)"
fi

# -----------------------------------------------------------------------------
# Plugins (Autosuggestions before Syntax Highlighting)
# -----------------------------------------------------------------------------
[[ -r /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh

[[ -r /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh

# -----------------------------------------------------------------------------
# SYN-OS Prompt
# -----------------------------------------------------------------------------
# user@host = red
# directory = dark red
# git branch = cyan
# time = white
# brackets = blue
# success ✔ = green | fail ✘ = red
# -----------------------------------------------------------------------------

autoload -Uz vcs_info

precmd() { vcs_info }

zstyle ':vcs_info:git:*' enable git
zstyle ':vcs_info:git:*' formats ' %F{cyan}(%b)%f'

PROMPT='%F{blue}[%f%F{red}%n@%m%f %F{160}%~%f${vcs_info_msg_0_} %F{white}%D{%H:%M:%S}%f%F{blue}]%f %(?.%F{green}✔%f.%F{red}✘%f) %# '

# -----------------------------------------------------------------------------
# Aliases
# -----------------------------------------------------------------------------
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias please='sudo'

# Only override if tools exist
command -v bat >/dev/null 2>&1 && alias cat='bat --paging=never'
command -v rg  >/dev/null 2>&1 && alias grep='rg'

alias synos="dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc"
alias redshirt='sudo zsh ~/.SYN-REDSHIRT.zsh'

# -----------------------------------------------------------------------------
# Functions
# -----------------------------------------------------------------------------
mkcd() {
  mkdir -p -- "$1" && cd -- "$1"
}

# =============================================================================
# End of SYN-OS Configuration
# =============================================================================
