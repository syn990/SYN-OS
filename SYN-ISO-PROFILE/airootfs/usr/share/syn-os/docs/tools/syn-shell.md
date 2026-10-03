# SYN-SHELL

SYN-SHELL is SYN-OS's own window onto the system: your files, the
archives among them, all in one place, laid out the way ranger lays out
a terminal. Three columns: the folder you came from, the folder you're
in, and a preview of whatever the cursor is on.

![SYN-SHELL main window](../screenshots/syn-shell-main-window.png)
*Placeholder, the main window.*

## Opening it

`Super+E`, or SYN-SHELL from the main menu, or the disk icon on the bar
(which opens it on the drive itself rather than your home folder).
Opening an archive from anywhere else (a browser download, a link) opens
it in SYN-SHELL too.

## Getting around

It's built for the keyboard, with ranger's keys, but the mouse works
everywhere: click to move, double-click to open, right-click for a menu
that lists every action with its key.

| Key | Does |
|---|---|
| `j` `k` or the arrows | Down, up |
| `l` or `Enter` | Open: go into a folder or an archive, open a file in its usual app |
| `h` or `Backspace` | Up to the parent folder |
| `H` `L` | Back, forward |
| `gg` `G` | Top, bottom |
| `gh` `gr` `gm` | Home, `/`, your mounted drives |
| `/` then `n` `N` | Search, next, previous |
| `zh` or `Ctrl+H` | Show or hide hidden files |
| `Ctrl+L` | Type a path (or click the path at the top) |
| `S` | Open foot in this folder |
| `?` | Every key, in the preview column |

The strip along the top shows where you are, your position in the list,
how much you've marked and how full the drive is. The line along the
bottom is the file under the cursor the way `ls -l` would print it.

## Files

Actions work on what you've marked (`Space` marks and moves on), or on
the file under the cursor if nothing is marked.

| Key | Does |
|---|---|
| `yy` / `dd` / `pp` | Copy, cut, paste (also `Ctrl+C` `Ctrl+X` `Ctrl+V`) |
| `cw` or `F2` | Rename |
| `dD` or `Delete` | Delete, after asking |
| `F7` | New folder |
| `yp` | Copy the path |

Copy and paste go through the system clipboard, so you can copy here and
paste into another app, or the other way round.

Copying a shortcut (a symlink) copies the shortcut, not what it points
to, and deleting one never touches the real file. Moving to another
drive copies first and removes the original only once that's done. It
won't paste a folder into itself.

## Archives

zip, 7z, rar, tar in any compression, iso, deb, rpm, cab and a lone
`.gz` or `.xz` file all open like folders: `l` goes in, `h` comes back
out. Moving the cursor onto one shows what's inside before you open it.
Files inside preview too.

| Key | Does |
|---|---|
| `X` on an archive | Extract it here. One folder inside comes out as that folder; anything else lands in a new folder named after the archive |
| `X` inside one | Extract what's marked next to the archive |
| `yy` inside one | Copy out, then `pp` wherever you like |
| `:compress name.tar.zst` | Pack what's marked into a new archive (`.zip`, `.7z` and the other tar types work too) |
| `r` | Open in its usual app instead |
| `Esc` | Cancel an extract or compress that's running |

Password-protected archives ask for the password. Nothing in an archive
can write outside the folder you extract it into.

## Commands

`:` opens a command line at the bottom: `:cd PATH`, `:mkdir NAME`,
`:touch NAME`, `:rename NAME`, `:extract [WHERE]`, `:compress NAME`,
`:term`, and `:!command` to run something in foot here.
