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

# Clear terminal before displaying splash screen
clear

# =============================================================================
# SYN-OS ASCII Branding
# =============================================================================
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

# =============================================================================
# System Information and Checks
# =============================================================================

# Internet check
if ! ping -c 1 -W 1 archlinux.org &>/dev/null; then
  printf "\n\033[1;31m[Warning] No internet connection detected.\033[0m\n"
  printf "   - Connect Ethernet or use: \033[35miwctl\033[0m for Wi-Fi\n"
  printf "   - For WWAN modems: \033[35mmmcli\033[0m\n"
else
  printf "\n\033[1;32m[OK] Internet connection detected.\033[0m\n"
fi

# Storage overview
printf "\n\033[1;34m[System Storage] Available disks:\033[0m\n"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT

# CPU and memory (best effort)
command -v lscpu >/dev/null 2>&1 && { printf "\n\033[1;34m[CPU]\033[0m\n"; lscpu | sed -n '1,8p'; }
command -v free  >/dev/null 2>&1 && { printf "\n\033[1;34m[Memory]\033[0m\n"; free -h; }

# =============================================================================
#   WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA
# =============================================================================
printf "\n\033[1;41m WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA \033[0m\n"
printf "\033[1;31mReview and edit install scripts before running.\033[0m\n"
printf "Failure to do so will result in total data loss.\n"
printf "\n\033[1;33mEdit these before proceeding:\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage0.zsh\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage1.zsh\033[0m\n"

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

# Useful defaults
setopt histignorealldups sharehistory incappendhistory
setopt prompt_subst
setopt auto_menu complete_in_word
setopt nocaseglob
setopt interactivecomments
unsetopt beep

# History location (survives if user writes to persistent overlay)
HISTFILE="${XDG_STATE_HOME:-$HOME/.local/state}/zsh/history"
HISTSIZE=50000
SAVEHIST=50000

# Completion
autoload -Uz compinit bashcompinit
zmodload -i zsh/complist
compinit -d "${XDG_CACHE_HOME:-$HOME/.cache}/zsh/zcompdump"

# Keybinds
bindkey -e
bindkey '^H' backward-kill-word
bindkey '^[[3;5~' kill-word

# Tools (guarded)
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

# Installer alias
alias syntax990="/root/syn-resources/scripts/syn-stage0.zsh"

# Quick helpers
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias please='sudo'
alias cat='bat --paging=never' 2>/dev/null || true
alias grep='rg' 2>/dev/null || true
mkcd() { mkdir -p "$1" && cd "$1"; }

# =============================================================================
# Prompt: user@host red, dir dark red, git cyan, time white, box blue
# =============================================================================
autoload -Uz vcs_info
precmd() { vcs_info }

zstyle ':vcs_info:git:*' formats '(%b)'

PROMPT='%F{blue}[%f%F{red}%n@%m%f %F{160}%~%f ${vcs_info_msg_0_} %F{white}%D{%H:%M:%S}%f %F{blue}]%f %(?.%F{green}✔%f.%F{red}✘%f) %# '

# One line MOTD for the live ISO shell
if [[ -z ${SYN_MOTD_SHOWN} ]]; then
  export SYN_MOTD_SHOWN=1
  print -P "[SYN-OS Live] Zsh ready. Type 'syntax990' to start the installer."
fi
