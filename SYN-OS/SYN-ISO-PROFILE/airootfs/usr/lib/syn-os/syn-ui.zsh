#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         U I   &   A S C I I   H E L P E R S
#
#   Terminal color palette and printed banners/ASCII art used throughout the
#   SYN-OS installer stages. No install logic lives here — output only.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-UI (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

# --- palette ---------------------------------------------------------------
# Dark red/black theme throughout the installer, matching SYN-OS-RED.
#   accent  — bright red bold, section headers / banners
#   dim     — muted dark-red, separators and secondary labels
#   value   — plain/white, the actual data (device paths, sizes)
#   ok      — warm amber/gold, success
#   err     — bold red, failures
#   crit    — bold red + blink, reserved for the one truly destructive prompt
RESET=$'\e[0m'
C_ACCENT=$'\e[1;31m'   # bright red bold
C_DIM=$'\e[0;31m'      # dark red
C_VALUE=$'\e[0;37m'    # light gray/white
C_OK=$'\e[1;33m'       # amber/gold
C_ERR=$'\e[1;31m'      # bold red

# Stage accents — mark which install stage a motd/banner is printed from,
# distinct from the accent/dim/value/ok/err roles above.
C_LIVE=$'\e[1;35m'     # purple  — live installer shell, before synos-install runs
C_STAGE0=$'\e[1;34m'   # blue    — syn-stage0.zsh, pre-chroot (disk/pacstrap)
C_STAGE1=$'\e[1;31m'   # red     — syn-stage1.zsh, inside the chroot (matches C_ACCENT — the new system IS the brand)
C_CRIT=$'\e[5;1;31m'   # blink + bold red

syn_col_red()   { print -Pn "%{\e[0;31m%}$*%{\e[0m%}"; }
syn_col_green() { print -Pn "%{\e[0;32m%}$*%{\e[0m%}"; }
syn_col_bold()  { print -Pn "%{\e[1m%}$*%{\e[0m%}"; }
syn_ui::clear() { clear || printf "\n%.0s" {1..3}; }

# syn_ui::step "Partitioning disk" — themed header printed before a noisy
# command's own raw output, so that output is visually framed rather than
# muted. Pair with syn_ui::step_done/syn_ui::step_fail afterwards.
syn_ui::step() {
  local msg="$1" sep width
  printf "\n%s==>%s %s%s%s\n" "$C_ACCENT" "$RESET" "$C_VALUE" "$msg" "$RESET"
  width=$((${#msg} + 4))
  printf -v sep "%*s" "$width" ""
  printf "%s%s%s\n" "$C_DIM" "${sep// /-}" "$RESET"
}
syn_ui::step_done() {
  printf "%s✓%s %s%s%s\n" "$C_OK" "$RESET" "$C_DIM" "${1:-done}" "$RESET"
}
syn_ui::step_fail() {
  printf "%s✗ %s%s\n" "$C_ERR" "${1:-failed}" "$RESET" >&2
}
syn_ui::info() {
  printf "%s•%s %s%s%s\n" "$C_DIM" "$RESET" "$C_VALUE" "$1" "$RESET"
}
syn_ui::error() {
  printf "%sERROR:%s %s%s%s\n" "$C_ERR" "$RESET" "$C_VALUE" "$1" "$RESET" >&2
}

# syn_ui::doas <command> [args...] — runs `doas`, framed like every other
# step. doas reads the password straight from /dev/tty (no stdin, no way
# to theme the prompt itself or PAM's own retry loop), so this only styles
# what's actually ours: the announce line before, and a clear failure line
# after if doas exits non-zero (wrong password three times, ^C, or not
# permitted — doas doesn't distinguish, so neither do we).
syn_ui::doas() {
  syn_ui::step "Authenticating (doas)"
  if doas "$@"; then
    syn_ui::step_done "Authenticated"
    return 0
  else
    local rc=$?
    syn_ui::step_fail "doas denied — wrong password, cancelled, or not permitted"
    return $rc
  fi
}

syn_ui::face() {
  syn_ui::clear
  cat <<'EOF'

(((((((((((((((((((((((((/((((((((/***//////////////////////////////////////////
(((((((((((((((((((((((((/**(((/*******/////////////////////////////////////////
((((((((((((((((((((((((((***,,,,,,,,,,,,,,,,*****//////////////////////////////
((((((((((((((((((/********,,,,,,,,,,,,,,,,,,,,,,**/////////////////////////////
(((((((((((((((/****,,**,**,,,,,,,,,,,,,,,,,,,,,,,,****/////////////////////////
((((((((((/****,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,,********/////////////////////////
((((((((**********,,,*/******,,,,*,,,,*,,,,,,,,,,,,**//////////////////////////
((((((((********,,,**///(((((((((//**,,,,,,,,,,,,,,,,,,*////////////////////////
(((((((((******,,,,*/((((((((((//(//*****,,,,,,,,,,,,,,***/**////////////////////
(((((((//****,,,,,,/((((((((((//(*****,,,,,,,,,,,,,,***/**/////////////////////
((((((((/****,,,***(((((((/*****(*********,,,,,,,,,,,,,,,*//////////////////////
(((((((((/********(((((/###(((/****//(/(//*******,,,,,,,,**/////////////////////
(((((((((********((((((((*,..*(/(/**/(*((***,,,,,*,,,,,,,,,***//////////////////
(((((((((((******(((((///(((//***/////*,....,.,**/,,,,*/////////////////////////
((((((((((((/(***((((((/////////////*****,******/*,,,*//////////////////////////
((((((((((((/**/*((((((((///////(((((//*//******/*,,**//////////////////////////
((((((((((((/*((*(((##(((/((((((///////**////***/**/////////////////////////////
((((((((((((/*/***(((((((((((////,,(((/****//////,//////////////////////////////
((((((((((((((/*****/(((((((((//*,,,,,,,***//////*///////////*//////////////////
((((((((((((((********(((((***,,,,,,,,,,,,**///////////////****/////////////////
((((((((((((((**********/(/**/////*****,,*******///////////****//////////////***
(((((((((((((((*,,,,,,****,////////,,,,****,*,,////////////*****/*////////******
(((((((((((((((//*,,,,,,,,,,***,****,,,,,,,******************//***************
*,,,,,***********,****************************************/****//***************
*,,,,,***********,****************************************/****//***************
,,,,,,,,,,*********,,,,,,,,,,***,****,,,,,,,******************//***************

Without constraints; SYN-OS has independent freedom and creative intelligence.

EOF
  sleep 0.2
  syn_ui::clear
}

# syn_ui::confirm_wipe <disk> — interactive y/n gate before anything
# destructive happens. Returns 0 only on an explicit "y"/"yes" answer typed
# at the prompt; anything else (including plain enter) is a "no". This is
# always shown unless RequireWipeConfirm=no in synos.conf — that default is
# deliberately "yes, ask" and is not meant to be turned off casually.
syn_ui::confirm_wipe() {
  local disk="$1" answer
  printf "\n%s%s%s\n" "$C_DIM" "${disk}:" "$RESET"
  lsblk "$disk" -o NAME,SIZE,MODEL,FSTYPE,LABEL,MOUNTPOINTS 2>/dev/null \
    | sed "s/^/  /" \
    || printf "  %s(unable to read disk info)%s\n" "$C_ERR" "$RESET"
  printf "\n%s!%s %sThis will %serase everything%s on %s%s%s.%s\n" \
    "$C_CRIT" "$RESET" "$C_VALUE" "$C_ERR" "$C_VALUE" "$C_ACCENT" "$disk" "$C_VALUE" "$RESET"
  printf "%sIf that's not the disk you meant, stop now (Ctrl+C).%s\n" "$C_DIM" "$RESET"
  printf "%sProceed and wipe %s%s%s? [y/N] %s" "$C_VALUE" "$C_ACCENT" "$disk" "$C_VALUE" "$RESET"
  read -r answer </dev/tty
  case "${answer:l}" in
    y|yes) return 0 ;;
    *) return 1 ;;
  esac
}

syn_ui::pacman_snack() {
  cat <<'EOF'
⠀⠀⠀⠀⣀⣤⣴⣶⣶⣶⣦⣤⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⢿⣿⣿⣷⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⢀⣾⣿⣿⣿⣿⣿⣿⣿⣅⢀⣽⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⣼⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠛⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠛⠁⠀⠀⣴⣶⡄⠀⣴⣶⡄⠀⣴⣶⡄
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣀⠀⠙⠛⠁⠀⠙⠛⠁⠀⠙⠛⠁
⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠙⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠈⠙⠿⣿⣿⣿⣿⣿⣿⣿⠿⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠉⠉⠉⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
EOF
}

syn_ui::intro_montage() {
  syn_ui::clear
  printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
  printf "     _______.____    ____ .__   __.          ______        _______.\n"
  printf "    /       |\\   \\  /   / |  \\ |  |         /  __  \\      /       |\n"
  printf "   |   (----  \\   \\/   /  |   \\|  |  ______|  |  |  |    |   (---- \n"
  printf "    \\   \\      \\_    _/   |  .    | |______|  |  |  |     \\   \\    \033[0m\n"
  printf "\033[0;31m.----)   |       |  |     |  |\\   |        |   --'  | .----)   |   \033[0m\n"
  printf "\033[0;31m|_______/        |__|     |__| \\__|         \\______/  |_______/    \033[0m\n"
  printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
  sleep 1
  echo "SYN-OS Stage 0 running in pre-chroot context."
  sleep 1
  syn_ui::clear
}

syn_ui::end_summary() {
  # 7th arg is PartitionStrat, not firmware type — uefi-bootctl and mbr-grub
  # both have a separate boot partition; mbr-syslinux doesn't (BootPart ==
  # RootPart there, see syn-disk.zsh's partitionStrat_mbr_syslinux).
  local root_part="$1" root_mnt="$2" boot_part="$3" boot_mnt="$4" boot_fs="$5" root_fs="$6" partition_strat="$7"
  syn_ui::clear
  printf "\n%s✓ SUMMARY:%s %sStage 0 complete. Proceeding to Stage 1.%s\n\n" "$C_OK" "$RESET" "$C_VALUE" "$RESET"
  printf "%s•%s Root: %s%s%s mounted at %s%s%s\n" "$C_DIM" "$RESET" "$C_ACCENT" "$root_part" "$RESET" "$C_ACCENT" "$root_mnt" "$RESET"
  if [ "$partition_strat" != "mbr-syslinux" ]; then
    printf "%s•%s Boot: %s%s%s mounted at %s%s%s (fs=%s)\n" "$C_DIM" "$RESET" "$C_ACCENT" "$boot_part" "$RESET" "$C_ACCENT" "$boot_mnt" "$RESET" "$boot_fs"
  fi
  printf "%s•%s Root FS: %s%s%s\n" "$C_DIM" "$RESET" "$C_ACCENT" "$root_fs" "$RESET"
  printf "%s•%s fstab generated, packages installed, scripts copied.%s\n\n" "$C_DIM" "$RESET" "$RESET"
  sleep 2
}

syn_ui::final_banner() {
  cat <<'EOF'

████████████████████████████████████████████████████████████████████████████████████████████████
                                                                                              ██
  ██████▓██   ██▓ ███▄    █  ▒█████    ██████                                                 ██
▒██    ▒ ▒██  ██▒ ██ ▀█   █ ▒██▒  ██▒▒██    ▒                                                 ██
░ ▓██▄    ▒██ ██░▓██  ▀█ ██▒▒██░  ██▒░ ▓██▄                                                   ██
  ▒   ██▒ ░ ▐██▓░▓██▒  ▐▌██▒▒██   ██░  ▒   ██▒      ______ _  _  _ _   _        _________     ██ 
▒██████▒▒ ░ ██▒▓░▒██░   ▓██░░ ████▓▒░▒██████▒▒      \  ___) || || | \ | |      / _ \  ___)    ██
▒ ▒▓▒ ▒ ░  ██▒▒▒ ░ ▒░   ▒ ▒ ░ ▒░▒░▒░ ▒ ▒▓▒ ▒ ░       \ \  | \| |/ |  \| |_____| | | \ \       ██
░ ░▒  ░ ░▓██ ░▒░ ░ ░░   ░ ▒░  ░ ▒ ▒░ ░ ░▒  ░ ░        > >  \_   _/|     (_____) | | |> >      ██
░  ░  ░  ▒ ▒ ░░     ░   ░ ░ ░ ░ ░ ▒  ░  ░  ░         / /__   | |  | |\  |     | |_| / /__     ██
      ░  ░ ░              ░     ░ ░        ░        /_____)  |_|  |_| \_|      \___/_____)    ██
         ░ ░                                                                                  ██
                                                                                              ██
                          01010011 01011001 01001110 00101101 01001111 01010011               ██
                                                                                              ██
                                           SYN-OS: The Syntax Operating System                ██
 ####  #### #   #      ###   ####                                                             ██
#     #   # #   #     #   # #                                                                 ██
#      #### ##### ### #   # #              Created By: ----¬                                  ██
#      #  # #   #     #   # #                              :                                  ██
 #### #   # #   #      ###   ####                          :                                  ██
                                                          ===                                 ██
                                                                                              ██
███████ ██    ██ ███    ██ ████████  █████  ██   ██  █████   █████   ██████                   ██
██       ██  ██  ████   ██    ██    ██   ██  ██ ██  ██   ██ ██   ██ ██  ████                  ██
███████   ████   ██ ██  ██    ██    ███████   ███    ██████  ██████ ██ ██ ██                  ██
     ██    ██    ██  ██ ██    ██    ██   ██  ██ ██       ██      ██ ████  ██                  ██
███████    ██    ██   ████    ██    ██   ██ ██   ██  █████   █████   ██████                   ██
                                                                                              ██
████████████████████████████████████████████████████████████████████████████████████████████████

EOF
  cat <<'EOF'

SUMMARY: Stage One Complete, Congratulations!

You have successfully installed SYN-OS.

Please ensure your BIOS/UEFI or VM boots from the installed disk.
To reboot the system, type: reboot

EOF
}