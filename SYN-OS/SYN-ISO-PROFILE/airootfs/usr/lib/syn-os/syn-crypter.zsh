#!/usr/bin/env zsh
# SYN-CRYPTER.ZSH

# This script provides a unified interface for encrypting and decrypting files using multiple encryption algorithms:
# AES-256, Blowfish, RSA, and Redshirt (XOR-based obfuscation).

set -o errexit -o nounset -o pipefail

marker1="REDSHIRT"$'\0'
marker2="REDSHRT2"$'\0'
markersize=9
hashsize=20

# --- Embed the C helper directly in the script (for XOR) ---------------------

# C code for XOR transformation (Redshirt encryption/decryption)
redshirt_c_code() {
    cat <<'EOF'
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
}

# Build or locate C helper
helper="${TMPDIR:-/tmp}/syn-redshirt-core"

if [[ ! -x "$helper" ]]; then
    # Write the C code for Redshirt encryption/decryption
    redshirt_c_code > "${helper}.c"
    cc -O2 -o "$helper" "${helper}.c" || { echo "C compilation failed"; exit 1; }
    rm -f "${helper}.c"  # Clean up the C code
fi

# --- Helper: Redshirt Encryption (XOR) -----------------------------
redshirt_encrypt() {
    local file="$1"
    local tmpfile="$2"
    echo "Encrypting $file using Redshirt (XOR)..."
    
    # Use the C helper for XOR encryption
    "$helper" e < "$file" > "$tmpfile" || { echo "Redshirt encryption failed"; exit 1; }
}

redshirt_decrypt() {
    local file="$1"
    local tmpfile="$2"
    echo "Decrypting $file using Redshirt (XOR)..."
    
    # Use the C helper for XOR decryption
    "$helper" d < "$file" > "$tmpfile" || { echo "Redshirt decryption failed"; exit 1; }
}

# --- Helper: AES Encryption (AES-256) -----------------------------------------
aes_encrypt() {
    local file="$1"
    local password="$2"
    local tmpfile="${file}.aes_encrypted"
    echo "Encrypting $file with AES-256..."
    local salt iv
    salt=$(openssl rand -hex 16)
    iv=$(openssl rand -hex 16)

    openssl enc -aes-256-cbc -salt -in "$file" -out "$tmpfile" -pass pass:"$password" -pbkdf2 -S "$salt" -iv "$iv" || { echo "AES encryption failed"; exit 1; }
    
    dd if=<(echo -n "$salt$iv") of="$tmpfile" bs=1 seek=0 conv=notrunc || { echo "Failed to add salt/IV"; exit 1; }
    mv "$tmpfile" "$file"
}

aes_decrypt() {
    local file="$1"
    local password="$2"
    local tmpfile="${file}.aes_decrypted"
    echo "Decrypting $file with AES-256..."
    
    local salt_iv
    salt_iv=$(head -c 32 "$file") || { echo "Failed to read salt/IV"; exit 1; }
    local salt="${salt_iv:0:16}"
    local iv="${salt_iv:16:16}"

    openssl enc -aes-256-cbc -d -in "$file" -out "$tmpfile" -pass pass:"$password" -pbkdf2 -S "$salt" -iv "$iv" || { echo "AES decryption failed"; exit 1; }
    
    mv "$tmpfile" "$file"
}

# --- Helper: Blowfish Encryption ----------------------------------------------
blowfish_encrypt() {
    local file="$1"
    local password="$2"
    local tmpfile="${file}.blowfish_encrypted"
    echo "Encrypting $file with Blowfish..."
    openssl enc -bf -in "$file" -out "$tmpfile" -pass pass:"$password" -salt || { echo "Blowfish encryption failed"; exit 1; }
    mv "$tmpfile" "$file"
}

blowfish_decrypt() {
    local file="$1"
    local password="$2"
    local tmpfile="${file}.blowfish_decrypted"
    echo "Decrypting $file with Blowfish..."
    openssl enc -bf -d -in "$file" -out "$tmpfile" -pass pass:"$password" -salt || { echo "Blowfish decryption failed"; exit 1; }
    mv "$tmpfile" "$file"
}

# --- Helper: RSA Encryption ----------------------------------------------------
rsa_encrypt() {
    local file="$1"
    local rsa_pub_key="$2"
    local tmpfile="${file}.rsa_encrypted"
    echo "Encrypting $file with RSA..."
    openssl rsautl -encrypt -inkey "$rsa_pub_key" -pubin -in "$file" -out "$tmpfile" || { echo "RSA encryption failed"; exit 1; }
    mv "$tmpfile" "$file"
}

rsa_decrypt() {
    local file="$1"
    local rsa_priv_key="$2"
    local tmpfile="${file}.rsa_decrypted"
    echo "Decrypting $file with RSA..."
    openssl rsautl -decrypt -inkey "$rsa_priv_key" -in "$file" -out "$tmpfile" || { echo "RSA decryption failed"; exit 1; }
    mv "$tmpfile" "$file"
}

# --- Parse Arguments ----------------------------------------------------------
usage() {
    echo "Usage: $0 --encrypt --aes <password> <file>"
    echo "       $0 --decrypt --aes <password> <file>"
    echo "       $0 --encrypt --blowfish <password> <file>"
    echo "       $0 --decrypt --blowfish <password> <file>"
    echo "       $0 --encrypt --rsa <public_key.pem> <file>"
    echo "       $0 --decrypt --rsa <private_key.pem> <file>"
    echo "       $0 --encrypt --redshirt <file>"
    echo "       $0 --decrypt --redshirt <file>"
    exit 1
}

# --- Main Logic ---------------------------------------------------------------
if [[ $# -lt 2 ]]; then
    usage
fi

action=""
encryption=""
password=""
key=""
file=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --encrypt|--decrypt)
            action="$1"
            shift
            ;;
        --aes)
            encryption="aes"
            password="$2"
            shift 2
            ;;
        --blowfish)
            encryption="blowfish"
            password="$2"
            shift 2
            ;;
        --rsa)
            encryption="rsa"
            key="$2"
            shift 2
            ;;
        --redshirt)
            encryption="redshirt"
            file="$2"
            shift 2
            ;;
        *)
            file="$1"
            shift
            ;;
    esac
done

if [[ -z "$file" ]]; then
    echo "No file provided!"
    usage
fi

# Determine the mode based on action
if [[ "$action" == "--encrypt" ]]; then
    mode="encrypt"
elif [[ "$action" == "--decrypt" ]]; then
    mode="decrypt"
else
    echo "Invalid action. Please specify --encrypt or --decrypt."
    exit 1
fi

# --- Encryption Process -------------------------------------------------------
tmpfile="${file}.tmp"

# Handle the encryption method based on the user input
case "$encryption" in
    aes)
        if [[ "$mode" == "encrypt" ]]; then
            aes_encrypt "$file" "$password"
        elif [[ "$mode" == "decrypt" ]]; then
            aes_decrypt "$file" "$password"
        fi
        ;;
    blowfish)
        if [[ "$mode" == "encrypt" ]]; then
            blowfish_encrypt "$file" "$password"
        elif [[ "$mode" == "decrypt" ]]; then
            blowfish_decrypt "$file" "$password"
        fi
        ;;
    rsa)
        if [[ "$mode" == "encrypt" ]]; then
            rsa_encrypt "$file" "$key"
        elif [[ "$mode" == "decrypt" ]]; then
            rsa_decrypt "$file" "$key"
        fi
        ;;
    redshirt)
        if [[ "$mode" == "encrypt" ]]; then
            redshirt_encrypt "$file" "$tmpfile"
        elif [[ "$mode" == "decrypt" ]]; then
            redshirt_decrypt "$file" "$tmpfile"
        fi
        mv "$tmpfile" "$file"
        ;;
    *)
        echo "Unknown encryption type: $encryption"
        exit 1
        ;;
esac

echo "$action $encryption encryption of $file complete."