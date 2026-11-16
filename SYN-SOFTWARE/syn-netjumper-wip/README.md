# SYN-NETJUMPER

The SYN-OS web browser. `make && make install`, then `syn-netjumper [ADDRESS...]`.

## Who made it, and what it is built on

Invented by William Hayward-Holland (Syntax990) and written by the Syntax
Corporation. There is a PC version (this folder) and an Android version
(`SYN-APK/syn-netjumper`).

It is not a fork of another browser, and it is not Chromium. The browser
itself is the code in `netjumper.c`: the window, tabs, address bar, downloads,
find, history, bookmarks, saved passwords, site permissions, the bad-site
list, the fingerprint defence and the theming. Pages are drawn by
[WebKitGTK](https://webkitgtk.org/), the GTK port of the WebKit engine (the
same family as Safari and GNOME Web, not Google's Blink), in its own sandboxed
processes. The window is GTK 3. Video and sound go through GStreamer. It
contains no code from surf, the browser in `syn-surf-wip`.

## What talks to whom

Everything runs on this computer. SYN-NETJUMPER's own code contacts exactly
two outside services, and nothing else:

| What | When | What it sends |
|---|---|---|
| DuckDuckGo (`duckduckgo.com`) | when you type a search (not an address) in the bar | your search words |
| malware-filter (`malware-filter.gitlab.io`) | once a day, or when you press UPDATE NOW | nothing about you: it downloads two whole lists |

- No Google services. WebKitGTK has no Safe Browsing look-ups, no usage
  statistics and no crash reports, and SYN-NETJUMPER adds none.
- The bad-site check happens on this computer, against the downloaded list.
  The addresses you visit are never sent anywhere to be checked.
- Your history, bookmarks, cookies, site permissions and the bad-site list are
  files in `~/.local/share/syn-netjumper`. Saved passwords are in the desktop
  keyring. Nothing is synced anywhere.
- The sites you open are, of course, contacted by WebKit, along with whatever
  those sites load (their own fonts, scripts, adverts). Third-party cookies are
  refused, and WebKit's tracking prevention is on.
- The daily list update can be switched off in LIBRARY > BAD SITES ("Update the
  list by itself once a day"). Then the list only changes when you press UPDATE
  NOW or run `syn-netjumper --update-bad-sites`.

## What it does

- **Tabs**: new, close, reorder by dragging, middle-click to close. Links a page
  opens in a new window become tabs, but only when you clicked for one.
- **The address bar** takes an address or a search, and suggests from your history.
- **Bookmarks**: BOOKMARK (or Ctrl+D) keeps the page, and pressing it again
  removes it. They show on the new-tab page and in LIBRARY.
- **LIBRARY** (the button, Ctrl+Shift+O for bookmarks, Ctrl+H for history) is one
  window with five pages:
  - BOOKMARKS: open or remove.
  - HISTORY: search, open, remove one, or clear it all.
  - PASSWORDS: every saved login, with the password hidden until you press SHOW;
    remove one; IMPORT FROM CHROME.
  - SITE PERMISSIONS: what you allowed or blocked for each site. FORGET makes the
    site ask again.
  - BAD SITES: how many sites are on the list, when it was last updated, UPDATE
    NOW, and the daily-update switch.
- **Site permissions**: a site that wants your camera, microphone, location,
  screen or notifications asks first, in a bar under the page: ALLOW (Alt+A) or
  BLOCK (Alt+B). The answer is kept for that site. Anything else a page asks
  for is refused.
- **Bad-site warning**: a site on the list (malware from URLhaus; phishing from
  OpenPhish, PhishTank and IPThreat, about 36,000 domains) gets a full-page
  warning instead. GO BACK (SAFE) leaves, and GO THERE ANYWAY lets that one site
  through until you close the browser.
- **Saved passwords**, kept in the desktop keyring (the same Secret Service the
  rest of SYN-OS uses), never in a file of its own. A login form is filled from
  what's saved for that exact address, read from the tab itself and never from
  what the page claims. Only the main page takes part: a frame from some other
  site inside it can't ask for the login or raise a "save password?" prompt.
  Import from another browser with Chrome's own export, from LIBRARY or:

      syn-netjumper --import-passwords ~/Downloads/Google\ Passwords.csv

  SYN-NETJUMPER only reads the CSV Chrome itself writes (Chrome asks for your
  computer password to make it); it never touches Chrome's encrypted store.
- **Terminus everywhere**: the browser's bars and every web page are drawn in
  Terminus. Pages use "Terminess Nerd Font" (Terminus as outlines, so it scales
  to any size) and fall back to the bitmap Terminus. Icon fonts (Font Awesome,
  Material Icons and similar) are left alone so icons still show.
- **Downloads** go straight to your Downloads folder and never replace a file.
- Find on the page, zoom, full screen and WebKit's page inspector.
- It wears the desktop's live SYN-OS theme and changes when the desktop does.
  YouTube gets the same palette, with the adverts, Shorts and recommendations
  hidden, and a SYN button that opens the page in SYN-TUBE.

Keys are listed on the new-tab page.

## Fingerprint defence

Sites like browserleaks.com show how a browser can be recognised without
cookies, from what it draws and reports. SYN-NETJUMPER runs a script before
any page script, in every frame, that blurs these:

| Test | What a site gets |
|---|---|
| Canvas | the real picture with a tiny noise added: the same for one site all session, different for every site and every session |
| WebGL | "WebKit" / "WebKit WebGL" instead of your graphics card's name |
| Audio | read-backs carry the same kind of tiny noise |
| Fonts | canvas text is always Terminus and page text is forced to Terminus, so measuring text can't tell which fonts you have |
| CPU | `navigator.hardwareConcurrency` is always 4 |
| WebRTC | switched off, so it can't give away your local or public address |

What it does **not** hide: your IP address (use a VPN or Tor for that), the
user agent (WebKitGTK on Linux is rare, so it stands out), the screen size,
your time zone and language, and the TLS fingerprint of the network library.
Checked on 16 Nov 2026 with a local test page: the canvas hash changed between
two launches, the WebGL name and CPU count read as above, and
`RTCPeerConnection` was undefined.

## Building

Arch packages (other distributions have the same libraries under other names):

| Needed for | Packages |
|---|---|
| Building | `gcc`, `make`, `pkgconf` |
| The browser | `gtk3`, `webkit2gtk-4.1`, `libsecret`, `libsoup3`, `glib2` (pkg-config names `gtk+-3.0`, `webkit2gtk-4.1`, `libsecret-1`, `libsoup-3.0`) |
| Saved passwords | a Secret Service keyring running on the desktop (KDE's `ksecretd`, `gnome-keyring` or `keepassxc`) |
| Fonts | `terminus-font` and `ttf-terminus-nerd` |
| Video and sound | `gst-plugins-good`, `gst-plugins-bad`, `gst-libav` |
| Camera and microphone | `pipewire` and `gst-plugin-pipewire` |
| Location | `geoclue`. Not installed on this machine, so location fails even when you allow it. Installing it means GeoClue sends the names of nearby Wi-Fi networks to its location service. |

Tested with WebKitGTK 2.54.2, GTK 3.24.52, libsoup 3.8.1 and libsecret 0.21.8.

    make                 # builds ./syn-netjumper
    make install         # into ~/.local (PREFIX=/usr for system-wide)

Command line:

    syn-netjumper [ADDRESS...]              open addresses (in the running window if there is one)
    syn-netjumper --import-passwords FILE   import Chrome's password export into the keyring
    syn-netjumper --update-bad-sites        download the bad-site list now

## Not there yet

- Syncing bookmarks or passwords between the PC and the phone.
- Exporting bookmarks or passwords.
- Not tested on a real site: logging in through a frame from another site
  (on purpose, only the main page is filled), video calls (they need WebRTC,
  which is off), and location (geoclue isn't installed).

This folder ends in `-wip` so the ISO builders leave it alone; renaming it to
`-src` with a PKGBUILD and a `desktop/order` recipe is what would ship it.
