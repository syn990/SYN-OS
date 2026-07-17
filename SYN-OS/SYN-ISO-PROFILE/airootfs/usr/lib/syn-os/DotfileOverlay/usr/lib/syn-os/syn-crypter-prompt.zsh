#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                     S Y N - C R Y P T E R - P R O M P T
#
#   rofi front-end for syn-crypter.zsh: collects encrypt/decrypt,
#   algorithm, file, and whatever the algorithm needs (password for AES/
#   Blowfish, a key file for RSA, nothing extra for Redshirt), then runs
#   the real work inside syn_popup::run so the terminal window closes
#   itself when it's done instead of sitting open forever.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-CRYPTER-PROMPT (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

action="$(printf '%s\n' "Encrypt" "Decrypt" | syn_pick::rofi "Action:")"
[[ -n "$action" ]] || exit 0
action_flag="--$(print -l -- "$action" | tr '[:upper:]' '[:lower:]')"

algo="$(printf '%s\n' "AES-256" "Blowfish" "RSA" "Redshirt" | syn_pick::rofi "Algorithm:")"
[[ -n "$algo" ]] || exit 0

file="$(syn_pick::rofi_input "File:" "$HOME/")"
[[ -n "$file" ]] || exit 0
file="${file/#\~/$HOME}"

# $1=label (e.g. "Encrypt AES-256"), $2=basename for the toast, $3..=the
# real syn-crypter.zsh argv. Runs it, toasts success/failure, exits its rc.
notify_cmd='
  label="$1" base="$2"
  shift 2
  zsh /usr/lib/syn-os/syn-crypter.zsh "$@"
  rc=$?
  if (( rc == 0 )); then
    notify-send "Crypter" "$label succeeded: $base" 2>/dev/null || true
  else
    notify-send -u critical "Crypter" "$label failed: $base" 2>/dev/null || true
  fi
  exit $rc
'

case "$algo" in
  AES-256)
    pass="$(syn_pick::rofi_password "AES password:")"
    [[ -n "$pass" ]] || exit 0
    syn_popup::run zsh -c "$notify_cmd" -- "$action AES-256" "${file:t}" "$action_flag" --aes "$pass" "$file"
    ;;
  Blowfish)
    pass="$(syn_pick::rofi_password "Blowfish password:")"
    [[ -n "$pass" ]] || exit 0
    syn_popup::run zsh -c "$notify_cmd" -- "$action Blowfish" "${file:t}" "$action_flag" --blowfish "$pass" "$file"
    ;;
  RSA)
    key_prompt="Public key (.pem) for encrypt:"
    [[ "$action" == "Decrypt" ]] && key_prompt="Private key (.pem) for decrypt:"
    key="$(syn_pick::rofi_input "$key_prompt" "$HOME/")"
    [[ -n "$key" ]] || exit 0
    key="${key/#\~/$HOME}"
    syn_popup::run zsh -c "$notify_cmd" -- "$action RSA" "${file:t}" "$action_flag" --rsa "$key" "$file"
    ;;
  Redshirt)
    syn_popup::run zsh -c "$notify_cmd" -- "$action Redshirt" "${file:t}" "$action_flag" --redshirt "$file"
    ;;
esac
