# syn-surf

surf, forked. **Not built yet as syn-surf.** Everything in this folder
apart from this file is surf 2.1 exactly as released:
`https://dl.suckless.org/surf/surf-2.1.tar.gz`, sha256
`72e582920ba25a646203e93c2d2331d87f03037a28894d6c7e99af00ee043257`, the
same tarball and hash `SYN-LFS/desktop/pkgs/surf` pins. Diffing this
folder against that tarball shows everything SYN has changed. surf's own
readme is `README` and its license is `LICENSE` (MIT/X Consortium), which
stays with the code.

## What it adds to surf

**A capability file per site.** `~/.config/syn-os/surf/sites/<domain>`,
one capability per line:

```
javascript on
cookies first-party
webgl off
media ask
camera ask
microphone ask
third-party block
```

A site with no file gets nothing: no JavaScript, no cookies, no
third-party requests. When a page asks for something its file doesn't
answer, you're asked once and the answer is written as a line, so the
file is the whole record of what that site may do. surf already has the
places to hook this:

- `setparameter()` applies settings per page. surf's own `uriparams[]` in
  `config.def.h` does per-site settings from regexes at compile time;
  this moves that to files read at run time.
- `permissionrequested()` sees camera, microphone, location and
  notification requests.
- `decideresource()` and `decidenavigation()` see what's loaded and
  where a page goes.

**A request ledger.** surf's web extension (`webext-surf.c`) runs inside
WebKit's web process and already talks to surf over a socket
(`msgsurf()`). Hooking the page's `send-request` signal there sees every
request the page makes, blocks the ones the site's file says to, and
reports each one back. surf writes them to
`$XDG_RUNTIME_DIR/syn-surf/<pid>.ledger`, one line per request: time,
allowed or blocked, first- or third-party, URL. The bar shows the focused
page's counts the way it shows relay's state, by reading a file.

**Browse as another machine.** One window's traffic goes out through a
paired machine, so the web sees that machine's network. surf gives each
window its own `WebKitWebContext`, and WebKit takes proxy settings per
website data manager
(`webkit_website_data_manager_set_network_proxy_settings`), so one window
can go through a proxy while the others don't. Until SYN-RELAY has a
channel for it, the proxy is `ssh -D` to the node, the same SSH route
`syn-relay --stream-app` already uses.

**Save as Markdown.** One key saves the page's readable text as Markdown
under `~/Documents/SYN-SURF/`. The text comes out through the DOM in the
web extension, so it works with the page's JavaScript turned off.

**SYN look and sound.** Colors from the active theme (`syn_theme_load()`
from syn-crypter-src), tones through `syn_bar_notify_meaning()`, prompts
through syn-uplink-dialpad.

## Build order

1. Bring SYN-LFS's two `sed` patches into the source: `config.mk` on
   `webkit2gtk-4.1` and `webkit2gtk-web-extension-4.1`, and
   `gdk_set_allowed_backends("x11")` before `gtk_init()`. Then
   `pkgs/surf` can build this folder instead of the tarball, and the fork
   is live with no new features yet.
2. Capability files, read at run time, applied through `setparameter()`.
3. The ledger, in `webext-surf.c`.
4. The prompt for capabilities a site's file doesn't answer.
5. Save as Markdown.
6. Browse as another machine.

## Open questions

- **The prompt.** syn-uplink-dialpad has no yes/no mode yet (see
  `../WIP.md`).
- **How it's built.** surf uses its own `Makefile` and `config.mk`, not
  CMake, so neither `syn_tool` nor the ISO builders' loop can build it.
  Either it gets a `CMakeLists.txt` before it's renamed to `-src`, or
  `pkgs/surf` keeps building it with `make`.
- **Wayland.** surf runs under Xwayland, because SYN-LFS forces GTK's X11
  backend. Its address bar and find go through X11 window properties and
  dmenu (`surf.c`, `config.def.h`), so going native Wayland means
  replacing that path, not just dropping the `sed`.
