
#!/bin/zsh
# =============================================================================
#                             SYN-OS .zshrc
#       Live ISO Environment Configuration & Installation Splash Screen
# -----------------------------------------------------------------------------
#   Branded Zsh setup for the SYN-OS live environment. Provides system info,
#   installer hints, custom prompt, and useful defaults. Fast and self-contained.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

# Only clear and render splash for interactive TTYs
if [[ -o interactive && -t 1 ]]; then
  clear
fi

# =============================================================================
# SYN-OS ASCII Branding
# =============================================================================
if [[ -o interactive && -t 1 ]]; then
cat << "EOF"
 ▄▄▄▄▄▄▄▄▄▄▄  ▄         ▄  ▄▄        ▄               ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄
▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░▌      ▐░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌
▐░█▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌▐░▌░▌     ▐░▌             ▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀▀▀
▐░▌          ▐░▌       ▐░▌▐░▌▐░▌    ▐░▌             ▐░▌       ▐░▌▐░▌
▐░█▄▄▄▄▄▄▄▄▄ ▐░█▄▄▄▄▄▄▄█░▌▐░▌ ▐░▌   ▐░▌ ▄▄▄▄▄▄▄▄▄▄▄ ▐░▌       ▐░▌▐░█▄▄▄▄▄▄▄▄▄
▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░▌  ▐░▌  ▐░▌▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░░░░░░░░░░▌
 ▀▀▀▀▀▀▀▀▀█░▌ ▀▀▀▀█░█▀▀▀▀ ▐░▌   ▐░▌ ▐░▌ ▀▀▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌ ▀▀▀▀▀▀▀▀▀█░▌
          ▐░▌     ▐░▌     ▐░▌    ▐░▌▐░▌             ▐░▌       ▐░▌          ▐░▌
 ▄▄▄▄▄▄▄▄▄█░▌     ▐░▌     ▐░▌     ▐░▐░▌             ▐░█▄▄▄▄▄▄▄█░▌ ▄▄▄▄▄▄▄▄▄█░▌
▐░░░░░░░░░░░▌     ▐░▌     ▐░▌      ▐░░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌
 ▀▀▀▀▀▀▀▀▀▀▀       ▀       ▀        ▀▀               ▀▀▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀▀▀

   Without constraints; SYN-OS has independent freedom, volition,
   and creative intelligence, actively participating in the
   ongoing creation of reality.
EOF
fi

# =============================================================================
# System Information and Checks
# =============================================================================

# [WHH:+] Network check: prefer HTTPS HEAD via curl
if curl -fsSIL --max-time 2 https://archlinux.org >/dev/null 2>&1; then
    printf "\n\033[1;32m[OK] Internet connection detected.\033[0m\n"
  else
    printf "\n\033[1;31m[Warning] No internet connection detected.\033[0m\n"
    printf "   - Connect Ethernet or use: \033[35miwctl\033[0m for Wi‑Fi\n"
    printf "   - For WWAN modems: \033[35mmmcli\033[0m\n"
  fi

# Storage overview
printf "\n\033[1;34m[System Storage] Available disks:\033[0m\n"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT

# CPU and memory (best effort)
if command -v lscpu >/dev/null 2>&1; then
  printf "\n\033[1;34m[CPU]\033[0m\n"; lscpu | sed -n '1,8p'
fi
if command -v free >/dev/null 2>&1; then
  printf "\n\033[1;34m[Memory]\033[0m\n"; free -h
fi

# =============================================================================
#   WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA
# =============================================================================
printf "\n\033[1;41m WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA \033[0m\n"
printf "\033[1;31mReview and edit install scripts before running.\033[0m\n"
printf "Failure to do so will result in total data loss.\n"
printf "\n\033[1;33mEdit these before proceeding:\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage0.zsh\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage1.zsh\033[0m\n"

# [WHH:+] Optional guard function: call `confirm_syntax990` if you want an interactive safety check
confirm_syntax990() {
  printf "\n\033[1;41m DANGER \033[0m This will erase target disks configured in syn-stage scripts.\n"
  printf "Type \033[1msyntax990 YES\033[0m to proceed: "
  read -r a b
  if [[ "$a $b" == "syntax990 YES" ]]; then
    /root/syn-resources/scripts/syn-stage0.zsh
  else
    printf "Aborted.\n"
  fi
}

# =============================================================================
# Installation
# =============================================================================
printf "\n\033[1;33m[Install] To begin installing SYN-OS:\033[0m\n"
printf "   - Run \033[38;2;23;147;209msyntax990\033[0m to start the installer.\n"

# =============================================================================
# Custom Build Notes
# =============================================================================
printf "\n\033[1;36m[Customization]\033[0m Create a pre-configured ISO by adjusting:\n"
printf "   \033[35mSYN-ISO-PROFILE\033[0m and \033[35mBUILD-SYNOS-ISO.zsh\033[0m\n"

# =============================================================================
# Credits
# =============================================================================
printf "\n\033[1;34m[Credits] SYN-OS by William Hayward-Holland (Syntax990)\033[0m\n"
printf "   GitHub: https://github.com/syn990/SYN-OS\n\n"

# =============================================================================
# Environment
# =============================================================================
export LANG=en_GB.UTF-8
export EDITOR='nano'

# =============================================================================
# Zsh Options
# =============================================================================
setopt histignorealldups sharehistory incappendhistory
setopt prompt_subst
setopt auto_menu complete_in_word
setopt nocaseglob
setopt interactivecomments
unsetopt beep
setopt extended_history             # timestamp + duration in history
setopt hist_ignore_space            # commands starting with space aren't saved
setopt nomatch                      # error when globs don't match (safer in scripts)
setopt path_dirs                    # cd into $PATH dirs by name

# =============================================================================
# History
# =============================================================================
: "${XDG_STATE_HOME:=${HOME}/.local/state}"
HISTDIR="${XDG_STATE_HOME}/zsh"
mkdir -p "$HISTDIR"
HISTFILE="${HISTDIR}/history"
HISTSIZE=50000
SAVEHIST=50000

# =============================================================================
# Completion
# =============================================================================
autoload -Uz compinit bashcompinit
zmodload -i zsh/complist

# [WHH:+] Fast init; compaudit to reduce warnings/noise
# Create cache dir for zcompdump
: "${XDG_CACHE_HOME:=${HOME}/.cache}"
ZSH_CACHE="${XDG_CACHE_HOME}/zsh"
mkdir -p "$ZSH_CACHE"
if compaudit | grep -q .; then
  # Not fixing perms automatically in live ISO; just force init
  compinit -C -d "${ZSH_CACHE}/zcompdump"
else
  compinit -d "${ZSH_CACHE}/zcompdump"
fi

# [WHH:+] Better completion UX
zstyle ':completion:*' menu select
zstyle ':completion:*' matcher-list 'm:{a-z}={A-Za-z}' 'r:|[._-]=* r:|=*'
zstyle ':completion:*' squeeze-slashes true

# =============================================================================
# Keybinds
# =============================================================================
bindkey -e
bindkey '^H' backward-kill-word
bindkey '^[^[[3;5~' kill-word 2>/dev/null || bindkey '^[[3;5~' kill-word

# =============================================================================
# Tools (guarded)
# =============================================================================
if command -v fzf >/dev/null 2>&1; then
  [[ -r /usr/share/fzf/completion.zsh ]] && source /usr/share/fzf/completion.zsh
  [[ -r /usr/share/fzf/key-bindings.zsh ]] && source /usr/share/fzf/key-bindings.zsh
fi

if command -v zoxide >/dev/null 2>&1; then
  eval "$(zoxide init zsh)"
fi

[[ -r /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh
[[ -r /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh

# =============================================================================
# Aliases and helpers
# =============================================================================
# Installer alias (destructive) – kept as-is for speed
alias syntax990="/root/syn-resources/scripts/syn-stage0.zsh"

alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias please='sudo'

# [WHH:+] Guarded tool replacements
if command -v bat >/dev/null 2>&1; then
  alias cat='bat --paging=never'
fi
if command -v rg >/dev/null 2>&1; then
  alias grep='rg'
fi

mkcd() { mkdir -p -- "$1" && builtin cd -- "$1"; }

# =============================================================================
# Prompt & VCS
# =============================================================================
autoload -Uz vcs_info
precmd() { vcs_info }

zstyle ':vcs_info:git:*' formats '%F{cyan}(%b%u%c)%f'  # show branch + staged/unstaged
zstyle ':vcs_info:git:*' actionformats '%F{cyan}(%b|%a)%f'
zstyle ':vcs_info:*' enable git

# [WHH:+] Shorten path; time & status on the right
setopt prompt_subst
PROMPT='%F{blue}[%f%F{red}%n@%m%f %F{160}%~%f ${vcs_info_msg_0_} %F{blue}]%f %# '
RPROMPT='%F{white}%D{%H:%M:%S}%f %(?..%F{red}✘%?%f:%F{green}✔%f)'

# =============================================================================
# One line MOTD for the live ISO shell
# =============================================================================
if [[ -o interactive && -t 1 && -z ${SYN_MOTD_SHOWN} ]]; then
  export SYN_MOTD_SHOWN=1
  print -P "[SYN-OS Live] Zsh ready. Type 'syntax990' to start the installer."
fi
