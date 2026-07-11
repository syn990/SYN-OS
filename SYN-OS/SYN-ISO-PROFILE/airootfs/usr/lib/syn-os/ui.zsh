#!/bin/zsh
# SYN‑OS UI & ASCII helpers (no logic, just vibes)
# /usr/lib/syn-os/ui.zsh

# --- palette ---------------------------------------------------------------
# Dark red/black theme throughout the installer, matching SYN-OS-RED. Roles
# are deliberately distinct rather than one flat color for every line:
#   accent  — bright red bold, section headers / banners
#   dim     — muted dark-red, separators and secondary labels
#   value   — plain/white, the actual data (device paths, sizes) so it reads
#             against the colored labels around it instead of blurring in
#   ok      — warm amber/gold, success (not green — doesn't fit the palette)
#   err     — bold red, failures
#   crit    — bold red + blink, reserved for the one truly destructive prompt
RESET=$'\e[0m'
C_ACCENT=$'\e[1;31m'   # bright red bold
C_DIM=$'\e[0;31m'      # dark red
C_VALUE=$'\e[0;37m'    # light gray/white
C_OK=$'\e[1;33m'       # amber/gold
C_ERR=$'\e[1;31m'      # bold red

# Stage accents — for the plain/static text (motd, welcome-shell banners)
# that used to sit outside this palette entirely (stock cyan/green, or one
# leftover off-brand blue). Not a repaint of the whole installer: syn_ui::*
# above keeps its red/amber/gold roles throughout every stage. These three
# are for text that specifically wants to say "you're in the live shell"
# vs "pre-chroot disk work" vs "inside the new system" at a glance, while
# staying inside one coherent dark palette instead of introducing a random
# hue.
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
  # Show what's actually on the disk right before asking — the prompt used
  # to just print the device path and trust the reader had already checked
  # it elsewhere. Someone moving fast (or who doesn't know lsblk exists) had
  # no way to catch a wrong Disk= in synos.conf from this prompt alone.
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
  # RootPart there, see syn-partition.zsh).
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