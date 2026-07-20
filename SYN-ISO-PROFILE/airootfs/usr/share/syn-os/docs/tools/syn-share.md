# SYN-SHARE

SYN-SHARE is SYN-OS's file-transfer hub: one dispatcher script fronting six
transfer protocols (rsync, Samba, NFS, HTTP, TFTP, netcat), each usable as
either a server (share files from this machine) or a client (pull from /
push to another SYN-SHARE-capable host or any standard server speaking the
same protocol). It has no daemon of its own — every "server" side is a real
system service (`rsyncd`, `smb`, `nfs-server`, ...) that SYN-SHARE
configures and starts on demand, and every "client" action is a thin
wrapper around the standard client tool (`rsync`, `mount -t cifs`, `wget`,
`tftp`, `nc`, `scp`).

## Components

| File | Role |
|---|---|
| `/usr/lib/syn-os/syn-share.zsh` | Subcommand dispatcher — `<subcommand> [args...]`, not meant to be run interactively |
| `/usr/lib/syn-os/syn-share-lib.zsh` | Every action function (`srv_start_rsync`, `cli_smb_mount`, etc.), sourced only |
| `/usr/lib/syn-os/syn-share-prompt.zsh` | rofi front-end: collects passwords/IPs/paths, then runs `syn-share.zsh` |
| `/usr/lib/syn-os/syn-pipe-share.zsh` | labwc pipe-menu generator — the SYN-SHARE submenu's actual content |
| `/usr/lib/syn-os/syn-bar-share-quickmenu.zsh` | waybar click-menu — a smaller rofi popup for one-click bar access |
| `/usr/lib/syn-os/syn-bar-share-status.zsh` | waybar `custom/synshare` module — active-service count as JSON |

## Protocols and directories

Every server side writes into its own subdirectory of `/srv/syn-share/`;
every client pull lands in `~/syn-share-pull`.

| Protocol | Server share dir | Port | Auth |
|---|---|---|---|
| rsync | `/srv/syn-share/rsync` | `8730` (non-standard, less noise) | rsyncd secrets file (`/etc/syn-share/rsync.secrets`), user `synshare` |
| Samba | `/srv/syn-share/samba` | 139/445 | Samba password for a dedicated `synshare` system user |
| NFSv4 | `/srv/syn-share/nfs` | 2049 | Subnet-restricted export, no auth beyond `hosts allow` |
| HTTP | `/srv/syn-share/http` | `8080` | None — read-only mirror, `busybox httpd` |
| TFTP | `/srv/syn-share/tftp` | 69/udp | None — `in.tftpd --secure` |
| Netcat | `/srv/syn-share/incoming` | `7000` | None — receives a `tar` stream and unpacks it in place |

Client-side mount points: `/mnt/syn-share-smb` (Samba), `/mnt/syn-share-nfs`
(NFS). All service and firewall state lives in these fixed paths; nothing
is configurable beyond what the prompt scripts ask for.

Every server start funnels through `srv_setup_dirs`, which creates all six
share directories plus `/etc/syn-share` and the log file `/var/log/syn-share.log`
in one pass (not just the directory for the protocol being started), and
opens the matching firewall rule via `nft` if present. `srv_setup_dirs`'s
first `doas` call is unconditional, always the first privilege prompt of any
server-start action — `ensure_pkg`'s own `doas` (installing a missing
package) only fires when the package isn't already present, so it can't be
relied on as the "first prompt" the way `srv_setup_dirs` can.

## Subcommands (`syn-share.zsh`)

```
Server:
  srv-start-rsync <pass>            srv-stop-rsync
  srv-start-samba <pass>            srv-stop-samba
  srv-start-nfs                     srv-stop-nfs
  srv-start-http                    srv-stop-http
  srv-start-tftp                    srv-stop-tftp
  srv-start-nc                      srv-stop-nc
  srv-otg-start <rsync-pass>
  srv-stop-all

Client:
  cli-set-server <ip>
  cli-rsync-pull <ip> <pass>
  cli-rsync-push <ip> <pass> <localpath>
  cli-smb-mount <ip> <pass>          cli-smb-umount
  cli-nfs-mount <ip> <0|1-ssh-tunnel> cli-nfs-umount
  cli-http-mirror <ip>
  cli-tftp-get <ip> <file>           cli-tftp-put <ip> <file>
  cli-nc-send <ip> <path>
  cli-ssh-copy <ip> <path> <dest>

  status                             (plain-text service status)
```

`srv-otg-start` is a one-shot convenience: it starts rsync and HTTP
together and prints both connection strings — meant for a quick "plug in
and grab files" session (OTG = on-the-go) without walking through each
protocol's own menu entry. `srv-stop-all` stops every server side in one
call.

`cli-set-server` probes a target IP with `nc -z` against every protocol's
port and writes the address to `~/.config/syn-os/syn-share-server`, which
every other client prompt then offers as its default IP — set once, reused
everywhere.

## NFS over SSH

`cli-nfs-mount <ip> 1` opens a background SSH tunnel (`ssh -fNL
20490:localhost:2049 <ip>`) and mounts through `localhost:20490` instead of
connecting to port 2049 directly — useful when the target's NFS port isn't
reachable but SSH is. `cli-nfs-mount <ip> 0` mounts directly with no
tunnel.

## Package installation on demand

No transfer-protocol package is a baseline SYN-OS install dependency;
`ensure_pkg` checks with `pacman -Qi` and installs on first use of that
protocol (`rsync`, `samba`, `nfs-utils`, `busybox`, `tftp-hpa`,
`openbsd-netcat`, `pv`). Installing runs `doas pacman -Sy --noconfirm
--needed <pkg>` with output captured to the SYN-SHARE log.

## Desktop integration

### labwc pipe-menu

**Applications > SYN-OS Tools > SYN-SHARE (file transfer hub)** is a live
labwc pipe-menu (`syn-pipe-share.zsh`), not a static list — see
[labwc](../labwc.md) for the menu structure this sits inside. It has two
submenus:

- **Server** — one toggle item per protocol. Each item's label reflects
  live `systemctl is-active` state (`● rsync — running (click to stop)` or
  `○ Start rsync`), so the menu always shows what's actually running rather
  than a static action list. Below the six protocol toggles: **OTG Quick
  Share** and **Stop ALL services**.
- **Client** — one item per client action (Set/Probe Server, rsync
  Pull/Push, Samba Mount/Unmount, NFS Mount direct/via SSH tunnel/Unmount,
  HTTP Mirror, TFTP Get/Put, Netcat Send, SSH Copy).

Below both submenus: **Service Status** (opens `foot` running `syn-share.zsh
status`) and **View Log** (`less /var/log/syn-share.log`).

Every item that needs input routes through `syn-share-prompt.zsh
<keyword>` — a single bare keyword, never a pre-built command string.
labwc's `<command>` element goes straight to `execvp()` with no shell in
between (see `labwc-actions(5)`), so anything with embedded quotes or
spaces passed directly through `menu.xml` risks silent mangling. All
password/IP/path prompt logic therefore lives in `syn-share-prompt.zsh`,
keyed by keyword, rather than being built inline in the menu generator.
Prompts appear via rofi popups (through `syn-picker-lib.zsh`) before `foot`
ever opens, so a password prompt never shows up as a blank terminal window
waiting for input.

### waybar quick-menu and status module

`custom/synshare` in [waybar](../waybar.md) shows an arrow-glyph count
(`⇄ N`) of currently-active SYN-SHARE services, computed by
`syn-bar-share-status.zsh` — it walks the same six units
(`rsyncd`, `smb`, `nfs-server`, `synshare-httpd`, `synshare-tftpd`,
`synshare-nc`), checks each with `systemctl is-active --quiet`, and emits
waybar's expected `{text, tooltip, class}` JSON. The module's `class`
becomes `active` whenever at least one service is up, and the tooltip lists
every service's up/down state on its own line. Text is empty (module
effectively hidden) when nothing is running.

Clicking the module runs `syn-bar-share-quickmenu.zsh`, a smaller,
separate rofi popup distinct from the full labwc submenu — one-click bar
access without leaving the current window to open the root menu. It offers
the same six Start/Stop toggle lines (mirroring live state exactly the way
`syn-pipe-share.zsh`'s own toggle logic does, so the two never show
different actions for the same underlying state), plus Set/Probe Server,
rsync Pull, Stop ALL services, and Service Status.

![The SYN-SHARE waybar quick-menu open](../screenshots/synshare-quickmenu.png)
*Placeholder — the quick-menu popup with its six Start/Stop toggle lines
plus Set/Probe Server, rsync Pull, Stop ALL services, and Service Status.*

## Logging

Every action logs a timestamped line to `/var/log/syn-share.log` — created
world-writable (`chmod 666`) by the first `doas` call so both privileged
service-management commands and unprivileged client actions can append to
the same file. **View Log** in the pipe-menu opens it directly in `less`.
