# Services Toggle

`syn-services-toggle.zsh` enables or disables any real systemd `.service`
unit installed on the machine — a live, general-purpose systemd control
panel reachable from the desktop menu, not a fixed shortlist of
"supported" services.

## Real units, not a hardcoded allowlist

Earlier versions of this tool offered exactly three units — `sshd`,
`bluetooth`, `qemu-guest-agent` — regardless of what was actually installed
on the machine. The script's own header states the concrete failure mode
that caused: a service like `postgresql`, `libvirtd`, or `ollama` enabled
on a real machine was completely invisible to the toggle, since it only
ever knew how to offer those three names. The current script instead
queries `systemctl` directly and lists **every** unit it finds, live, each
time the menu opens.

## How the listing works

```
systemctl list-unit-files --type=service --no-legend
```

is filtered to units whose enablement state is literally `enabled` or
`disabled` (via `awk '$2=="enabled" || $2=="disabled"'`). Units in
`static`, `alias`, or `indirect` state are deliberately excluded — the
script's own comment explains why: those activate only as a dependency of
something else and cannot be enabled or disabled directly, so listing them
would just add dead menu entries that error out when picked.

For each remaining unit, the script also queries its live `systemctl
is-active` state (`active`/`inactive`) and builds one line per unit:

```
name<TAB>enabled-state<TAB>active-state
```

`enabled-state` (`enabled`/`disabled`) is what the toggle actually acts on;
`active-state` is shown for context only — a unit can be enabled but not
currently running (or, less commonly, active without being enabled), and
the menu label surfaces both rather than conflating them.

## Toggle flow

1. A rofi list shows every unit as `<name> — <enabled-state>,
   <active-state>` (e.g. `sshd — enabled, active`), sized to 15 visible
   rows at a fixed 720px width.
2. The script offers **only the one action that unit's current state
   actually allows** — `Disable + Stop <name>` if it's currently enabled,
   `Enable + Start <name>` if it's currently disabled. There is never a
   dead "Disable" option shown on something already disabled, or vice
   versa.
3. A second rofi confirmation (`Disable + Stop <name>` / `Cancel`) guards
   against an accidental click before anything actually changes.
4. The real action — `doas systemctl <enable|disable> --now <unit>` — runs
   inside `syn_popup::run`, the same self-closing popup-terminal pattern
   used by [SYN-SHARE](./syn-share.md),
   [BlackArch Toggle](./blackarch-toggle.md), and the rest of SYN-OS's
   desktop tools.

## Toasts

On completion, the popup fires a `notify-send` toast: `"<name> enabled and
started."` / `"<name> stopped and disabled."` on success, or a
critical-urgency `"Failed: <name> (<enable|disable>)"` toast if the
`systemctl` call itself fails. See [Notifications](./notifications.md) for
how the underlying toast pipeline (mako) works — this doc only covers that
Services fires one.

## Desktop integration

**Preferences > Services (enable/disable any systemd unit)** in the
[labwc](../labwc.md) root menu runs `syn-services-toggle.zsh` directly —
no `foot` wrapper, since the rofi pickers are already their own centered
popups; only the final `doas systemctl` call gets a real terminal, via
`syn_popup::run`.
