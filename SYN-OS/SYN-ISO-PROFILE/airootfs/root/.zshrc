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
# Live Environment Notice (Gap 1) + Install Model (Gap 3)
# =============================================================================
if [[ -o interactive && -t 1 ]]; then
  printf "\n\033[1;36m[SYN-OS Live]\033[0m This is a \033[1mtemporary\033[0m installer shell, not your final system.\n"
  printf "Use it to \033[1medit config\033[0m and then run the installer.\n"

  printf "\n\033[1;36m[Install Model]\033[0m\n"
  printf "  • \033[1msyntax990\033[0m runs \033[1msyn-stage0.zsh\033[0m (pre-chroot): disk prep, pacstrap, arch-chroot.\n"
  printf "  • \033[1msyn-stage1.zsh\033[0m runs \033[1mautomatically inside arch-chroot\033[0m.\n"
  printf "  • \033[31mDo NOT run stage1 manually\033[0m — stage0 invokes it in the correct context.\n"
fi

# =============================================================================
# System Information and Checks
# =============================================================================

# Network check: prefer HTTPS HEAD via curl
if curl -fsSIL --max-time 2 https://archlinux.org >/dev/null 2>&1; then
  printf "\n\033[1;32m[OK] Internet connection detected.\033[0m\n"
else
  printf "\n\033[1;31m[Warning] No internet connection detected.\033[0m\n"
  printf "   - Connect Ethernet or use: \033[35miwctl\033[0m for Wi‑Fi\n"
  printf "   - For WWAN modems: \033[35mmmcli\033[0m\n"
fi

# =============================================================================
#   WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA
# =============================================================================
printf "\n\033[1;41m WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA \033[0m\n"
printf "\033[1;31mReview and edit install scripts before running.\033[0m\n"
printf "Failure to do so will result in total data loss.\n"
printf "\n\033[1;33mEdit these before proceeding:\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-disk-config.zsh\033[0m   # target disk, partitions, filesystems, mounts\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-packages.zsh\033[0m      # package arrays to be installed\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage0.zsh\033[0m        # orchestrator (you run this via 'syntax990')\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage1.zsh\033[0m        # auto-run inside arch-chroot (do NOT run directly)\n"

# Optional guard function: interactive safety check
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
# SYN‑OS Structure (Gap 4) + Resources Layout (Gap 5)
# =============================================================================
printf "\n\033[1;36m[SYN‑OS Structure]\033[0m\n"
printf "  • \033[1mScripts\033[0m: /root/syn-resources/scripts\n"
printf "      - \033[1msyn-disk-config.zsh\033[0m → set DISK (e.g. /dev/sda, /dev/nvme0n1, /dev/vda), partition layout,\n"
printf "        filesystems (ext4, btrfs, etc.) and mount points. Use defaults for flat-to-disk, or customise freely.\n"
printf "      - \033[1msyn-packages.zsh\033[0m → \033[1msingle source of truth\033[0m for package arrays installed on the target system.\n"
printf "      - \033[1msyn-stage0.zsh\033[0m → pre-chroot orchestrator (you invoke this via \033[1msyntax990\033[0m).\n"
printf "      - \033[1msyn-stage1.zsh\033[0m → post-chroot system config (runs automatically; \033[31mdo not run directly\033[0m).\n"
printf "  • \033[1mDotfiles & Overlays\033[0m:\n"
printf "      - /root/syn-resources/DotFileOverlay  → copied into the installed system.\n"
printf "      - /etc/skel                          → baseline user skeleton (extended by overlays).\n"
printf "  • \033[1mISO Profile & Build (acknowledgement)\033[0m:\n"
printf "      - Built with ArchISO; \033[1mSYN-ISO-PROFILE\033[0m and \033[1mBUILD-SYNOS-ISO.zsh\033[0m generate custom ISOs.\n"
printf "        (Build details exist, but are not required to install from this live ISO.)\n"
printf "  • \033[1mImportant\033[0m:\n"
printf "      - \033[1mEverything in /root/syn-resources describes the FINAL INSTALLED SYSTEM, not the live ISO.\033[0m\n"
printf "      - Includes configs, scripts, dotfiles, and a \033[1mtest template used by Syntax990\033[0m.\n"

# =============================================================================
# Installation
# =============================================================================
printf "\n\033[1;33m[Install]\033[0m To begin installing SYN-OS:\n"
printf "   - Run \033[38;2;23;147;209msyntax990\033[0m to start the installer.\n"

# =============================================================================
# Custom Build Notes (acknowledgement only)
# =============================================================================
printf "\n\033[1;36m[Customization]\033[0m You can generate a pre-configured ISO from this profile later via:\n"
printf "   \033[35mSYN-ISO-PROFILE\033[0m and \033[35mBUILD-SYNOS-ISO.zsh\033[0m (ArchISO-based).\n"

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
HISTFILE="${HISTDIR}/history}"
HISTSIZE=50000
SAVEHIST=50000

# =============================================================================
# Completion
# =============================================================================
autoload -Uz compinit bashcompinit
zmodload -i zsh/complist

# Fast init; compaudit to reduce warnings/noise
: "${XDG_CACHE_HOME:=${HOME}/.cache}"
ZSH_CACHE="${XDG_CACHE_HOME}/zsh"
mkdir -p "$ZSH_CACHE"
if compaudit | grep -q .; then
  # Not fixing perms automatically in live ISO; just force init
  compinit -C -d "${ZSH_CACHE}/zcompdump"
else
  compinit -d "${ZSH_CACHE}/zcompdump"
fi

# Better completion UX
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
# Tools
# =============================================================================
if command -v fzf >/dev/null 2>&1; then
  [[ -r /usr/share/fzf/completion.zsh   ]] && source /usr/share/fzf/completion.zsh
  [[ -r /usr/share/fzf/key-bindings.zsh ]] && source /usr/share/fzf/key-bindings.zsh
fi

if command -v zoxide >/dev/null 2>&1; then
  eval "$(zoxide init zsh)"
fi

[[ -r /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh       ]] && \
  source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh
[[ -r /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh

# =============================================================================
# Aliases and helpers
# =============================================================================
# Installer alias (destructive!)
alias syntax990="/root/syn-resources/scripts/syn-stage0.zsh"

alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
alias please='sudo'

# Guarded tool replacements
if command -v bat >/dev/null 2>&1; then
  alias cat='bat --paging=never'
fi
if command -v rg >/dev/null 2>&1; thenW
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

# Shorten path; time & status on the right
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