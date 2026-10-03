# SYN-SHELL

SYN-SHELL is SYN-OS's own window onto the system: your files, the
archives among them and a shell, all in one place, laid out the way
ranger lays out a terminal. Three columns: the folder you came from, the folder you're
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

Every file wears a SYN-OS icon saying what it is: a folder with its tab
cut at an angle, and a sheet with a clipped corner and a mark in the
theme's colour for code, scripts, images, sound, video, archives,
documents, keys and the rest (a disc for disk images, a box for
packages). `zi` turns them off for plain `ls -F` style names.

The middle column can show more about each file, each switched on or
off on its own, with titles over them so you can tell them apart. Your
choice is remembered, and the right-click menu has the same switches
under Fields.

| Key | Field |
|---|---|
| `zp` | Permissions, as `ls -l` prints them |
| `zo` | Owner |
| `zs` | Size (on to start with) |
| `zm` | Date modified |
| `zc` | Date created (shows `-` on drives that don't record it) |

Folders show `-` for their size until you ask: `du` (or Folder sizes on
the right-click menu) measures every folder here, or just the marked
ones, in the background, and fills the sizes in; the bottom line then
says how much is inside, and in how many files.

If the column gets too narrow for all of them, owner and created step
aside first so names keep enough room.

Hold `Ctrl` and turn the wheel to zoom. It zooms whatever is under the
mouse, the files or the shell, each on its own, and remembers both.
`Ctrl+=`, `Ctrl+-` and `Ctrl+0` do the same from the keyboard.

## Git

Inside a git repository, every file shows what git thinks of it, in
git's own letters (as `git status -s` prints them) at the end of its
name: `M` modified, `A` added, `R` renamed, `??` new, and a red `UU` for
a conflict. The left letter is what's staged, in the theme's colour, the
right one what isn't yet. A folder with changes somewhere inside gets a
dot, and anything git ignores is greyed out.

The top strip shows the branch, how far ahead (↑) or behind (↓) it is,
and counts: `+` staged, `~` changed, `?` new, `!` in conflict. The
bottom line spells out the file under the cursor ("git: modified,
staged"). It keeps up by itself, including after a commit in the shell
area. `zg` turns it all off.

## Tabs

`Ctrl+T` opens a new tab where you are, and from then on a row of tabs
sits under the top strip. Each tab keeps its own place and its own
back and forward. `Ctrl+Tab` and `Ctrl+Shift+Tab` go to the next and
previous, `Alt+1` to `Alt+9` straight to one, `Ctrl+W` closes one (or
middle-click it). Right-click a folder for "Open in new tab".

The strip along the top shows where you are, your position in the list,
how much you've marked and how full the drive is. The line along the
bottom is the file under the cursor the way `ls -l` would print it.

## Previews

The column on the right shows whatever the cursor is on: a folder's
contents, a picture, the text of a file, or a hex dump of anything that
isn't text. Code is coloured by language (over 300 of them, the same
engine Kate uses) in the theme's own colours, and the language is named
at the top.

## Files

Actions work on what you've marked (`Space` marks and moves on), or on
the file under the cursor if nothing is marked.

| Key | Does |
|---|---|
| `yy` / `dd` / `pp` | Copy, cut, paste (also `Ctrl+C` `Ctrl+X` `Ctrl+V`) |
| `cw` or `F2` | Rename |
| `dD` or `Delete` | Move to the trash (no question: `Ctrl+Z` brings it back) |
| `Shift+Delete` | Delete for good, after asking |
| `Ctrl+Z` | Undo the last trash, move, copy, rename or new file |
| `F7` | New folder |
| `yp` | Copy the path |
| `gt` | Open the trash |
| `O` | Open with: every app that can open it, the usual one first |
| `+x` / `-x` | Make executable, or not (`:chmod 755` or `:chmod g+w` for the rest) |

Copy and paste go through the system clipboard, so you can copy here and
paste into another app, or the other way round.

Copying, moving and deleting happen in the background, so the window
never stops while a big folder goes across. A jobs window opens by
itself for anything that takes more than a moment, showing each one's
progress, the file it's on, its speed and how long is left, with a
CANCEL for each; the JOBS button in the top strip opens it again later.
Cancelling never leaves a half-copied file behind.

To rename lots at once, mark them (or mark nothing, for the whole
folder) and press `cW`. Their names open one per line in your text
editor, right in the shell area. Change what you like, save and quit,
check the list of changes it shows you, and they're all renamed. Swapping
two names works, and `Ctrl+Z` puts every one back.

When names already exist where you paste, you're asked once: replace
them, keep both (the new one becomes "name (2)"), or skip them. Pasting
a copy into the folder it came from makes "name (2)" straight away.

The trash is the same one every other app uses. In it, the bottom line
says where each file came from; the right-click menu restores it there,
deletes it for good, or empties the trash.

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

## The shell

Press `` ` `` and your shell opens along the bottom of the window, in the
folder you're looking at, in the same colours and font as foot. Press it
again to tuck it away; it keeps running, and comes back exactly as you
left it.

The two halves follow each other. `cd` somewhere in the shell and the
files above go there too. Move around in the files and the shell
follows, but only while it's sitting at a prompt: if something is
running, it's left alone, and anything you'd half-typed stays on the
line.

| Key | Does |
|---|---|
| `` ` `` | Show or hide the shell |
| `` Ctrl+` `` | Move the keyboard between the files and the shell |
| `Ctrl+Shift+D` | Send the shell to its own foot window |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Copy, paste (selecting with the mouse copies too; middle-click pastes it) |
| `Shift+PgUp` / `Shift+PgDn` | Scroll back through what's gone past |

`Ctrl+Shift+D` doesn't start a new shell in foot. It's the same shell,
moved: whatever was running keeps running, your history and variables
come with it, and closing that foot window ends it like any other
terminal. The next `` ` `` starts a fresh one here.

`S` still opens a separate foot in the current folder, as before.

## Commands

`:` opens a command line at the bottom: `:cd PATH`, `:mkdir NAME`,
`:touch NAME`, `:rename NAME`, `:extract [WHERE]`, `:compress NAME`,
`:shell`, `:detach`, `:term`, and `:!command` to run something in foot
here.
