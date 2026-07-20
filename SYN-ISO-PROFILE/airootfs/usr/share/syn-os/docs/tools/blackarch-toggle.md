# BlackArch Toggle

SYN-OS ships with [BlackArch](https://blackarch.org) disabled by default —
its ~3000-package security-tools repository isn't part of the base
install. `syn-blackarch-toggle.zsh` turns it on or off live, on an
already-installed system, no reinstall required.

## Files

| File | Role |
|---|---|
| `/usr/lib/syn-os/syn-blackarch-toggle.zsh` | Enable/Disable logic, menu.xml self-editing |
| `/usr/lib/syn-os/syn-pipe-blackarch.zsh` | labwc pipe-menu listing installed BlackArch tools (only reachable once enabled) |

## What Enable actually does

Enabling is a real, multi-step system change, not a cosmetic toggle:

1. Downloads BlackArch's own `strap.sh` installer from
   `https://blackarch.org/strap.sh` (fails cleanly with a critical toast if
   `blackarch.org` isn't reachable).
2. Runs `strap.sh` under `doas` — this is BlackArch's own repo-registration
   script: it adds the `[blackarch]` block to `/etc/pacman.conf`, installs
   the BlackArch signing keyring, and runs `pacman -Syy` (a repo-database
   refresh only, never a full package upgrade).
3. Runs a **full system upgrade** (`doas pacman -Syu --noconfirm`) before
   installing anything from BlackArch. This step exists specifically
   because `strap.sh`'s own `-Syy` only refreshes repo databases: on a
   system that hasn't been updated since its ISO was built, BlackArch's
   packages are frequently built against newer core libraries than what's
   installed, and installing them straight after `strap.sh` hits pacman's
   classic partial-upgrade version-conflict trap. Syncing the whole system
   first avoids that failure mode.
4. Installs exactly three packages: **`set`**, **`metasploit`**,
   **`aircrack-ng`** — not the `blackarch-recon`/`blackarch-scanner`/
   `blackarch-webapp` package groups. The script's own header is explicit
   about why: those groups pull in `badkarma`/`vega` and their broken
   `webkit2gtk`/`libsoup` dependency chains.
5. Inserts a **BlackArch** application submenu into `~/.config/labwc/menu.xml`,
   anchored between two comment markers (`BLACKARCH-MENU-START`/`-END`)
   that exist in the template unconditionally but stay empty until Enable
   fires — so a system that has never enabled BlackArch advertises nothing
   about it in the menu. The insertion guard checks for
   `id="blackarch-pipe"` specifically (not the `BLACKARCH-MENU-START`
   substring, which already appears in the template's own explanatory
   comment and would otherwise make the guard match on every run and
   silently no-op the insertion).

## What Disable does

Reverses all of it: removes the three packages
(`doas pacman -Rns --noconfirm set metasploit aircrack-ng`), strips the
`[blackarch]` block back out of `/etc/pacman.conf`, and deletes the
menu.xml block between the same two markers. The repo, keyring
registration, and menu entry are all treated as one reversible unit.

## The BlackArch submenu (`syn-pipe-blackarch.zsh`)

Once enabled, **Applications > BlackArch** is a live labwc pipe-menu, not a
static list — it lists only the packages from that same fixed three-item
set (`set`, `metasploit`, `aircrack-ng`) that are actually
installed (checked with `pacman -Qi` at menu-open time), resolving each
package's real `/usr/bin` binary via `pacman -Ql` rather than assuming the
binary name matches the package name. If Enable partially failed and none
of the three ended up installed, the menu shows a single explanatory entry
pointing back at **Preferences > BlackArch > Enable**.

## Toggle UX

**Preferences > BlackArch** in the [labwc](../labwc.md) root menu runs
`syn-blackarch-toggle.zsh` directly — no `foot` wrapper, since the rofi
picker is already its own centered popup. It checks current state with
`pacman -Qi set` and shows a rofi list headed `BlackArch (currently
enabled/disabled):` offering **Enable BlackArch** / **Disable BlackArch**.

The chosen action re-invokes the same script with `--enable`/`--disable`
inside `syn_popup::run` — the same self-closing popup-terminal pattern used
by [SYN-SHARE](./syn-share.md), [SYN-CRYPTER](./syn-crypter.md), and
[SYN-GRAPHMAP](./syn-graphmap.md) — so the actual `doas`/`strap.sh`/`pacman`
work (which needs a real terminal, since `strap.sh` has its own
interactive prompts) gets a real window framed the same way as every other
SYN-OS tool, rather than running invisibly behind the rofi picker.

## Toasts

Both Enable and Disable fire `notify-send` toasts on completion —
`"Enabled — repo synced, set metasploit aircrack-ng installed. More at
blackarch.org"` or `"Disabled — packages and repo removed"` on success,
and a critical-urgency failure toast (`"Enable failed: couldn't reach
blackarch.org"` or `"... install error, see terminal"`) if a step fails.
See [Notifications](./notifications.md) for how the underlying toast
pipeline (mako) works.

After either action, the menu.xml change requires a reload — **Super+Escape**
(Reconfigure) — before the BlackArch entry appears or disappears from
Applications. See [labwc](../labwc.md) for that keybind and the rest of
the menu structure this submenu attaches to.
