#!/usr/bin/env zsh
# SYN-REDSHIRT.ZSH (Encryption Suite with Multiple Algorithms)
#
# Concept inspired by Introversion Software's Uplink "RedShirt" tool, this script provides a simple file encryptor/decryptor that uses a custom XOR-based method for obfuscation. 
# It is designed for fun and thematic consistency within SYN-OS, and is not intended for secure encryption. 
# Encrypted files are marked with a custom header and include a SHA1 hash for integrity verification. The Zsh script handles orchestration, while a compiled C helper performs the byte transformation for speed.

set -o errexit -o nounset -o pipefail
source /usr/lib/syn-os/syn-ui.zsh

marker1="REDSHIRT"$'\0'
marker2="REDSHRT2"$'\0'
markersize=9
hashsize=20

# --- build or locate C helper -------------------------------------------------
helper="${TMPDIR:-/tmp}/syn-redshirt-core"

if [[ ! -x "$helper" ]]; then
  cat > "${helper}.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    int encrypt = (argv[1][0] == 'e');
    int c;
    while ((c = getchar()) != EOF) {
        if (encrypt) putchar((c + 128) & 0xFF);
        else putchar((c - 128) & 0xFF);
    }
    return 0;
}
EOF
  cc -O2 -o "$helper" "${helper}.c"
  rm -f "${helper}.c"
fi

transform_bytes() {
  local mode="$1"
  if [[ "$mode" == "encrypt" ]]; then
    "$helper" e
  else
    "$helper" d
  fi
}

# --- usage -------------------------------------------------------------------
[[ $# -lt 1 ]] && { echo "Usage: $0 filename"; exit 1; }
file="$1"
[[ ! -f "$file" ]] && { syn_ui::error "File not found: $file"; exit 1; }

headbytes=$(dd if="$file" bs=$markersize count=1 2>/dev/null)
if [[ "$headbytes" == "$marker2" ]]; then
  mode="decrypt2"
elif [[ "$headbytes" == "$marker1" ]]; then
  mode="decrypt1"
else
  mode="encrypt"
fi

tmpfile="${file}.tmp"

# --- encrypt (always writes REDSHRT2+SHA1) -----------------------------------
if [[ "$mode" == "encrypt" ]]; then
  syn_ui::step "Encrypting $file"
  {
    printf "%s" "$marker2"
    dd if=/dev/zero bs=$hashsize count=1 2>/dev/null
    cat "$file" | transform_bytes encrypt
  } > "$tmpfile"

  # compute SHA1 of encrypted data
  tail -c +$((markersize+hashsize+1)) "$tmpfile" | sha1sum | awk '{print $1}' \
    | xxd -r -p | dd of="$tmpfile" bs=1 seek=$markersize conv=notrunc 2>/dev/null

  mv "$tmpfile" "$file"
  syn_ui::step_done "done"
fi

# --- decrypt v2 (with checksum) ----------------------------------------------
if [[ "$mode" == "decrypt2" ]]; then
  syn_ui::step "Decrypting $file (v2)"
  storedhash=$(dd if="$file" bs=1 skip=$markersize count=$hashsize 2>/dev/null | xxd -p -c $hashsize)
  datahash=$(tail -c +$((markersize+hashsize+1)) "$file" | sha1sum | awk '{print $1}')
  if [[ "$storedhash" != "$datahash" ]]; then
    syn_ui::error "checksum mismatch"
  fi
  dd if="$file" bs=1 skip=$((markersize+hashsize)) 2>/dev/null \
    | transform_bytes decrypt > "$tmpfile"
  mv "$tmpfile" "$file"
  syn_ui::step_done "done"
fi

# --- decrypt v1 (no checksum) ------------------------------------------------
if [[ "$mode" == "decrypt1" ]]; then
  syn_ui::step "Decrypting $file (v1, no checksum)"
  dd if="$file" bs=1 skip=$markersize 2>/dev/null \
    | transform_bytes decrypt > "$tmpfile"
  mv "$tmpfile" "$file"
  syn_ui::step_done "done"
fi
