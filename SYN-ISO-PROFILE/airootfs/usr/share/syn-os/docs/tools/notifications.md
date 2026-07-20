# Notifications

SYN-OS's toast notifications — the "Screenshot saved", "WiFi connected",
"BlackArch enabled" popups various scripts fire — are all rendered by
**mako**, the Wayland notification daemon. There is no other notification
system in SYN-OS: every `notify-send` call in the tree, without exception,
depends on mako being alive to actually render anything.

## Why mako has to start first

mako is launched from `~/.config/labwc/autostart`, labwc's single
session-entrypoint script (no shebang; runs under `/bin/sh`, not zsh — labwc
allows exactly one autostart file). The file's own header comment states the
ordering requirement directly:

> starts mako (backgrounded — every notify-send call in SYN-OS depends on it
> being alive, and it has to be up before anything fires a toast), then
> starts waybar.

The actual sequence at the end of `autostart` is:

```sh
apply_persisted_display_state
bootstrap_or_relaunch_theme
mako &
waybar
```

mako is started backgrounded (`mako &`) **before** `waybar` (which is not
backgrounded — it's the last line, running in the foreground as the
long-lived session process). This ordering matters because a `notify-send`
call made before mako has finished initializing has nothing to deliver to —
there's no notification daemon on the bus yet, so the toast silently goes
nowhere. Starting mako first, ahead of waybar and anything waybar's modules
might later trigger, ensures the daemon is already up by the time any
capture script, toggle script, or bar module click can fire a toast.

`apply_persisted_display_state` and `bootstrap_or_relaunch_theme` (display
output restore and theme bootstrap/wallpaper relaunch) run before mako, but
neither of those emits notifications itself, so the ordering constraint
doesn't apply to them — only capture happens after mako is backgrounded.

## Theming

mako is themed the same way every other themed component in SYN-OS is: via
a template rendered by `syn-theme-apply` from the live `.theme` file's
`SYN_*` variables. The full trickle-down mechanism (one `.theme` file →
`syn-theme-apply` → every app's own config format, re-rendered stateless on
every theme switch, no daemon) is documented in
[../theming/theme-engine.md](../theming/theme-engine.md) — this section only
covers mako's specific piece of it.

The template lives at
`/usr/lib/syn-os/theme-templates/mako-config.tmpl`:

```
background-color=SYN_PANEL
text-color=SYN_TEXT
border-color=SYN_ACCENT
progress-color=over SYN_ACCENT_DIM
border-size=3
border-radius=0
padding=16
margin=8
font=Terminus 11
default-timeout=5000
max-visible=3
layer=overlay
anchor=top-right

[urgency=critical]
border-color=SYN_URGENT
default-timeout=0
```

`SYN_PANEL`, `SYN_TEXT`, `SYN_ACCENT`, and `SYN_URGENT` are placeholder
tokens substituted with the current theme's actual hex values when
`syn-theme-apply` renders this template — they are not mako syntax
themselves. `over` on the `progress-color` line, by contrast, **is** real
mako config syntax (a compositing-operator keyword mako's config format
supports), left untouched by the template substitution.

Fixed (non-themed) values baked into the template: a 3px square-cornered
border, 16px padding, 8px margin between stacked notifications, `Terminus
11` font, a 5-second default timeout, a maximum of 3 notifications visible
at once, rendered on the `overlay` layer anchored to the top-right of the
screen. The `[urgency=critical]` section overrides the border color to
`SYN_URGENT` (the theme's dedicated error/urgent color) and sets
`default-timeout=0` — critical notifications (connection failures, install
errors) don't auto-dismiss; they stay up until the user clicks them away.

`syn-theme-apply` (`/usr/local/bin/syn-theme-apply`) renders this template
and reloads mako live on every theme switch:

```zsh
# -------- mako (notification toasts, live via makoctl reload) --------
render "$TEMPLATES_DIR/mako-config.tmpl" "$HOME/.config/mako/config"
makoctl reload 2>/dev/null || true
```

This is one of the components `syn-theme-apply` calls out as applying
**live** — its own closing summary groups mako with waybar and labwc as
taking effect immediately on theme switch, versus foot (new windows only)
or qt6ct/gtk3/superfile apps (next launch only).

## Notification-emitting scripts

Every `notify-send` call found under `/usr/lib/syn-os/` (there are none
under `/usr/local/bin/`):

| Script | Notifies about |
|---|---|
| `screenshot.zsh` | Screenshot saved, with the output file path |
| `screen-recorder.zsh` | Recording started (with output path) / stopped |
| `syn-bar-toggle-position.zsh` | Waybar moved to a new screen edge |
| `syn-services-toggle.zsh` | A systemd unit's enable/start/stop/disable action failed (critical), or succeeded |
| `syn-crypter-prompt.zsh` | An encrypt/decrypt operation succeeded or failed (critical), naming the operation and file |
| `syn-redshirt-prompt.zsh` | A redshirt operation succeeded or failed (critical), naming the file |
| `syn-blackarch-toggle.zsh` | BlackArch repo enable failed to reach blackarch.org (critical); package install failed (critical); enable succeeded; disable succeeded |

All of these degrade the same way if `notify-send` itself is missing or
fails: `notify-send ... 2>/dev/null || true` (or, in `syn-bar-wifi.zsh`'s
one case, `|| echo ... >&2` as a stderr fallback) — a missing/failed toast
never aborts the underlying operation, it's a best-effort side channel.

Critical-urgency calls (`notify-send -u critical`) are used consistently for
failure states across all of the above — connection failures, install
errors, failed toggles — which is what makes them stay pinned via the
`[urgency=critical]` / `default-timeout=0` mako rule described above rather
than auto-dismissing after 5 seconds like a normal success toast.

See [screenshot-and-recording.md](./screenshot-and-recording.md) and
[wifi.md](./wifi.md) for the full context of those scripts' toasts; BlackArch
and services toggles are documented in
[blackarch-toggle.md](./blackarch-toggle.md) and
[services-toggle.md](./services-toggle.md).

## Dependencies

`mako`, `makoctl` (mako's control CLI, part of the same package), and
whatever calls `notify-send` must have `libnotify` (or equivalent providing
the `notify-send` binary) available.
