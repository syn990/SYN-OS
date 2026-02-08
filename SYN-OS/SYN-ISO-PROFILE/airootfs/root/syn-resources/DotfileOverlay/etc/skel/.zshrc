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


# Locale and editor
export LANG=en_GB.UTF-8
export EDITOR='nano'

# Completion
autoload -Uz compinit bashcompinit
zmodload -i zsh/complist
compinit -d "${XDG_CACHE_HOME:-$HOME/.cache}/zsh/zcompdump"
setopt auto_menu complete_in_word

# Keybinds
bindkey -e
bindkey '^H' backward-kill-word
bindkey '^[[3;5~' kill-word

# Useful defaults
setopt prompt_subst
setopt nocaseglob
setopt interactivecomments
unsetopt beep

# Tools
if command -v fzf >/dev/null 2>&1; then
  [[ -r /usr/share/fzf/completion.zsh ]] && source /usr/share/fzf/completion.zsh
  [[ -r /usr/share/fzf/key-bindings.zsh ]] && source /usr/share/fzf/key-bindings.zsh
fi

if command -v zoxide >/dev/null 2>&1; then
  eval "$(zoxide init zsh)"
fi

# Autosuggestions then syntax highlighting
[[ -r /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh

[[ -r /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh

# -------------------------------------------------------------------
# SYN-OS Prompt
# user@host = red | directory = dark red | git = cyan | time = white
# brackets = blue | success ✔ = green | fail ✘ = red
# -------------------------------------------------------------------
autoload -Uz vcs_info
precmd() { vcs_info }

zstyle ':vcs_info:git:*' formats '(%b)'

PROMPT='%F{blue}[%f%F{red}%n@%m%f %F{160}%~%f ${vcs_info_msg_0_} %F{white}%D{%H:%M:%S}%f %F{blue}]%f %(?.%F{green}✔%f.%F{red}✘%f) %# '

# Aliases
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias please='sudo'
alias cat='bat --paging=never' 2>/dev/null || true
alias grep='rg' 2>/dev/null || true
alias synos="dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc"
alias redshirt=~~zsh ~/./.SYN-REDSHIRT.zsh # Introversion Software Encryption

mkcd() { mkdir -p "$1" && cd "$1"; }
