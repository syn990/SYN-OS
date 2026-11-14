# SYN-NETJUMPER

The SYN-OS web browser. `make && make install`, then `syn-netjumper [ADDRESS...]`.

## Who made it, and what it is built on

Invented by William Hayward-Holland (Syntax990) and written by the Syntax
Corporation. There is a PC version (this folder) and an Android version
(`SYN-APK/syn-netjumper`). The browser itself, meaning the tabs, address bar, downloads, find, history and
theming, is the code in `netjumper.c`. Pages are drawn by
[WebKitGTK](https://webkitgtk.org/), the GTK port of the WebKit engine, in its
own sandboxed processes, and the window is GTK 3. Video and sound go through
GStreamer. It contains no code from surf, the browser in `syn-surf-wip`.

## What it does

- Tabs: new, close, reorder by dragging, middle-click to close. Links a page
  opens in a new window become tabs, but only when you clicked for one.
- The address bar takes an address or a search, and suggests from your history.
- Downloads go straight to your Downloads folder and never replace a file.
- Find on the page, zoom, full screen and WebKit's page inspector.
- It wears the desktop's live SYN-OS theme and changes when the desktop does.
- Logins and site data are kept in `~/.local/share/syn-netjumper`, along with
  your history and the tabs to reopen next time. Third-party cookies are refused.
- **Saved passwords**, kept in the desktop keyring (the same Secret Service the
  rest of SYN-OS uses), never in a file of its own. A login form is filled from
  what's saved for that exact address, and submitting a new one offers to save
  it. Import the ones from another browser with Chrome's own export:

      syn-netjumper --import-passwords ~/Downloads/Google\ Passwords.csv

  SYN-NETJUMPER only reads the CSV Chrome itself writes (Chrome asks for your
  computer password to make it); it never touches Chrome's encrypted store.

Keys are listed on the new-tab page.

## Not there yet

Bookmarks, a history page, a window to browse and remove saved passwords, and
asking before a site uses the camera, microphone or location (WebKit's default
applies: those are refused).

This folder ends in `-wip` so the ISO builders leave it alone; renaming it to
`-src` with a PKGBUILD and a `desktop/order` recipe is what would ship it.
