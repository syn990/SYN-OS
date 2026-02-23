#!/usr/bin/env zsh
# SYN‑OS greetd + tuigreet setup (zsh) - not ready for use!!! use after stage1
set -e
set -u
set -o pipefail

info()  { print -P "%F{cyan}==> %f$*"; }
ok()    { print -P "%F{green}✓%f  $*"; }
warn()  { print -P "%F{yellow}⚠%f  $*"; }
fail()  { print -P "%F{red}✘%f  $*"; exit 1; }

# Re-exec as root if needed
if [[ $EUID -ne 0 ]]; then
  exec sudo -E zsh "$0" "$@"
fi

# Sanity checks
command -v pacman >/dev/null 2>&1 || fail "pacman not found. This script targets Arch/SYN‑OS."
command -v systemctl >/dev/null 2>&1 || fail "systemd required."

# Packages we need (adjust if you use a different compositor than labwc)
PACKAGES=(greetd greetd-tuigreet labwc dbus shadow sed coreutils)

info "Checking/Installing dependencies: ${PACKAGES[*]}"
missing=()
for p in "${PACKAGES[@]}"; do
  if ! pacman -Q "$p" >/dev/null 2>&1; then
    missing+=("$p")
  fi
done
if (( ${#missing[@]} )); then
  info "Installing: ${missing[*]}"
  pacman -Sy --needed --noconfirm "${missing[@]}" || fail "pacman install failed"
else
  ok "All dependencies already present"
fi

# Ensure helpful dirs exist
install -d -m 755 /usr/local/bin
install -d -m 755 /etc/greetd

info "Writing session launcher: /usr/local/bin/synos-session"
cat > /usr/local/bin/synos-session <<'SH'
#!/usr/bin/env bash
set -euo pipefail

# Wayland-friendly environment
export XDG_SESSION_TYPE=wayland
export MOZ_ENABLE_WAYLAND=1
export QT_QPA_PLATFORM=wayland
export SDL_VIDEODRIVER=wayland

# Change this to your compositor/WM if needed, e.g. sway, cage, river, etc.
SESSION_CMD="labwc"

# Run the session via dbus
exec dbus-run-session -- env XDG_SESSION_TYPE=wayland "${SESSION_CMD}"
SH

chmod 755 /usr/local/bin/synos-session
# strip CRLF just in case
sed -i 's/\r$//' /usr/local/bin/synos-session
ok "Session launcher ready"

# Ensure greeter user exists (use correct nologin path on Arch)
NOLOGIN="$(command -v nologin || echo /usr/bin/nologin)"
info "Ensuring greeter user exists"
if ! id -u greeter >/dev/null 2>&1; then
  useradd -r -M -s "$NOLOGIN" greeter
  ok "Created user: greeter"
else
  ok "User greeter already exists"
fi

info "Writing greetd config: /etc/greetd/config.toml"
cat > /etc/greetd/config.toml <<'CFG'
[terminal]
vt = 1

[default_session]
user = "greeter"
command = "tuigreet --cmd /usr/local/bin/synos-session"
CFG

chmod 644 /etc/greetd/config.toml
sed -i 's/\r$//' /etc/greetd/config.toml
ok "greetd config written"

info "Ensuring PAM config for greetd"
if [[ ! -f /etc/pam.d/greetd ]]; then
  cat > /etc/pam.d/greetd <<'PAM'
#%PAM-1.0
auth      include   system-login
account   include   system-login
password  include   system-login
session   include   system-login
PAM
  ok "Created /etc/pam.d/greetd"
else
  ok "PAM file /etc/pam.d/greetd already exists"
fi

info "Disabling getty on tty1"
systemctl disable --now getty@tty1.service >/dev/null 2>&1 || true

info "Enabling and restarting greetd"
systemctl enable --now greetd.service
systemctl daemon-reload
systemctl restart greetd

print
print "=========================================="
print "   🎉 DONE — tuigreet TUI login enabled!   "
print "   Switch to TTY1 with:   Ctrl+Alt+F1     "
print "=========================================="
EOF