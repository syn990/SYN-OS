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

case "$algo" in
  AES-256)
    pass="$(syn_pick::rofi_password "AES password:")"
    [[ -n "$pass" ]] || exit 0
    syn_popup::run zsh /usr/lib/syn-os/syn-crypter.zsh "$action_flag" --aes "$pass" "$file"
    ;;
  Blowfish)
    pass="$(syn_pick::rofi_password "Blowfish password:")"
    [[ -n "$pass" ]] || exit 0
    syn_popup::run zsh /usr/lib/syn-os/syn-crypter.zsh "$action_flag" --blowfish "$pass" "$file"
    ;;
  RSA)
    key_prompt="Public key (.pem) for encrypt:"
    [[ "$action" == "Decrypt" ]] && key_prompt="Private key (.pem) for decrypt:"
    key="$(syn_pick::rofi_input "$key_prompt" "$HOME/")"
    [[ -n "$key" ]] || exit 0
    key="${key/#\~/$HOME}"
    syn_popup::run zsh /usr/lib/syn-os/syn-crypter.zsh "$action_flag" --rsa "$key" "$file"
    ;;
  Redshirt)
    syn_popup::run zsh /usr/lib/syn-os/syn-crypter.zsh "$action_flag" --redshirt "$file"
    ;;
esac
