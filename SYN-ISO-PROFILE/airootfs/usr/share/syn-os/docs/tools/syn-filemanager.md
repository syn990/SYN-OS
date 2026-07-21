# File manager

SYN-OS ships its own file manager. It's small on purpose, a folder view
you can browse, rename, copy, cut, paste, and delete in, without extra
panes or features you'll never touch.

![syn-filemanager main window](../screenshots/syn-filemanager-main-window.png)
*Placeholder, the main window.*

## Opening it

`Super+E`, or File Browser from the main menu, or the disk icon on the
bar (which opens it rooted at the drive itself rather than your home
folder).

## Using it

Type a path into the bar at the top and press Enter to jump straight
there. Double-click a folder to go into it, double-click a file to open
it with whatever app you'd normally use for that kind of file.

| Key | Does |
|---|---|
| `Delete` | Delete the selected item |
| `F2` | Rename it |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy, cut, paste |
| `Alt+Up` | Go up one folder |

## Shortcuts and symlinks

Copying a shortcut (a symlink) copies the shortcut itself, not whatever
it points to, the same as you'd expect. Deleting one only removes the
shortcut, never the real file it points at.

Moving a file to a different drive works too. SYN-OS copies it across
first and only removes the original once that's finished, so an
interrupted move can't lose your file.

It won't let you paste a folder into itself, and it won't quietly delete
a file if you paste it back into the same folder it's already in.
