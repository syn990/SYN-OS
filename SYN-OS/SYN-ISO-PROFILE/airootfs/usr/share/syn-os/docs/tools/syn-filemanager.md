# syn-filemanager

syn-filemanager is SYN-OS's own file manager: a minimal Qt6 application
built around `QTreeView` + `QFileSystemModel`, replacing `pcmanfm-qt` as
the desktop's default file browser. Its `PKGBUILD` states the reason
directly: pcmanfm-qt has an unfixed resize/maximize bug, and rather than
work around a bug in someone else's application, SYN-OS ships a small
purpose-built replacement instead.

![syn-filemanager main window](../screenshots/syn-filemanager-main-window.png)
*Placeholder — the main window: path bar, QTreeView listing, and the
Up/Home/Rename/Delete/Copy/Cut/Paste toolbar, themed via qt6ct.*

## Source layout

```
/usr/lib/syn-os/syn-filemanager-src/
  CMakeLists.txt
  PKGBUILD
  resources/syn-filemanager.desktop
  src/
    main.cpp
    MainWindow.h / MainWindow.cpp
    FileOps.h   / FileOps.cpp
```

## What it does

A single-window file browser: a path bar (editable, `Enter` navigates),
a `QTreeView` showing the current directory's contents (name, size, type,
date — `QFileSystemModel`'s standard columns), and a toolbar with Up, Home,
Rename, Delete, Copy, Cut, Paste. Double-clicking a directory navigates
into it; double-clicking a file hands off to `QDesktopServices::openUrl`,
which defers to the system's XDG/MIME file association (`xdg-open` under
the hood) — syn-filemanager has no "open with" logic of its own.

Keyboard shortcuts: `Delete` (delete selection), `F2` (rename), the
platform `Copy`/`Cut`/`Paste` sequences (`Ctrl+C`/`Ctrl+X`/`Ctrl+V` on
Linux), and `Alt+Up` (navigate to parent directory).

The window opens at 1100×700, rooted at `$HOME` by default, or at the
directory/file passed as `argv[1]` (`syn-filemanager %f` in its
`.desktop` entry, so a file manager association or CLI invocation can
point it anywhere). See [labwc](../labwc.md) — the desktop launches it via
**Super+E** and the Applications menu's **File browser** entry.

## MainWindow / FileOps split

The codebase is deliberately split so that UI state and filesystem
mutation logic never tangle together:

- **`MainWindow`** owns the `QTreeView`/`QFileSystemModel` wiring, the
  toolbar, the path bar, keyboard shortcuts, navigation (`navigateUp`,
  `navigateHome`, `navigateToPathBar`), selection tracking, and an
  internal clipboard (`m_clipboard` + `m_clipboardIsCut`) for Copy/Cut/
  Paste. It has no filesystem-mutation code of its own — every action that
  actually touches disk (`deleteSelected`, `copySelected`→`pasteClipboard`,
  `cutSelected`→`pasteClipboard`) calls into `FileOps` instead.
- **`FileOps`** (a free-function namespace, not a class) implements
  `deleteEntries`, `copyEntries`, and `moveEntries` — confirmation dialogs,
  recursive directory handling, and per-entry error collection all live
  here, kept out of `MainWindow` so filesystem-error handling doesn't
  tangle into the UI class. Every `FileOps` entry point shows its own
  `QMessageBox` (confirmation up front, failure summary after) — callers
  don't need to inspect a return value to decide whether to warn the user.

A few concrete implementation details worth knowing:

- `QFileSystemModel` defaults to read-only, which silently disables inline
  rename; `MainWindow` explicitly calls `setReadOnly(false)`.
- No column uses Qt's `Stretch` resize mode. `MainWindow.cpp`'s own comment
  explains why: a stretch column resizes itself as a side effect of
  dragging a *neighboring* column's boundary, which reads as the drag
  direction being inverted. Every column is independently `Interactive`
  instead, so a resize only ever affects the boundary actually being
  dragged.
- `F2` rename is wired through an explicit `QShortcut` calling
  `m_view->edit()`, not `QTreeView`'s built-in edit-trigger handling for
  that key — `EditKeyPressed` is deliberately left out of
  `setEditTriggers` to avoid the two mechanisms racing each other.
- `isInlineEditorActive()` checks whether the currently focused widget is
  a descendant of the tree view but isn't the tree view itself — the
  inline rename editor is a real child `QWidget`, and without this check,
  `Delete`/shortcut-triggered actions fired while renaming would delete
  the selection instead of just editing its text (`QAbstractItemView`'s
  own editing-state flag is protected, so this ancestry check is the
  public substitute).

## Symlink-aware operations

`FileOps.cpp`'s own header comment states the policy plainly: copy
**recreates the symlink itself** (`readlink` the target, then create a new
symlink at the destination) rather than dereferencing it and copying
whatever it points to. Deleting a symlink removes the link, never its
target. The reasoning given: copying a broken or self-referential symlink
by dereferencing it would either fail outright or loop, and recreating the
link is what most users expect "copy" to do to a shortcut anyway.

This is implemented via `copySymlink()`, used consistently across all
three operations:

- **Delete** (`deleteEntries`) — a symlink is removed with `QFile::remove`,
  same as a plain file (never `removeRecursively`, which is reserved for
  real directories).
- **Copy** (`copyEntries`, and recursively inside `copyDirRecursive`) — a
  symlinked entry is recreated via `copySymlink`, not walked into as if it
  were a real directory. This also means a self-referential symlink inside
  a directory being copied can't cause infinite recursion — it's recreated
  as a link, not followed.
- **Move** (`moveEntries`) — tries `QFile::rename` first (same-filesystem,
  atomic); a symlink that can't be renamed falls back to `copySymlink` at
  the destination followed by removing the original link.

Two additional guards apply to Copy and Move alike, checked before any
filesystem mutation begins: `destinationIsInsideSource` refuses to copy or
move a directory into itself or one of its own subfolders (which would
otherwise make the recursive copy walk into a target it's simultaneously
reading as source), and `destinationIsSameAsSource` catches pasting a file
back into the folder it's already in with no rename (which would otherwise
have the copy logic delete the "existing destination" — actually the
source itself — before there's anything left to copy).

## Cross-filesystem move fallback

`moveEntries` tries `QFile::rename()` first for every entry — a
same-filesystem rename is atomic and fast. `QFile::rename()` doesn't expose
`errno` directly, so rather than trying to distinguish *why* a rename
failed, a failure is treated unconditionally as the classic **EXDEV**
case (source and destination on different filesystems — e.g. moving to a
different mount or a USB drive) and falls back to copy-then-delete-source:
copy the entry (via the same `copyFile`/`copySymlink`/`copyDirRecursive`
logic Copy uses) to the destination, then remove the original only if that
copy fully succeeded. If the copy side is incomplete, the original is left
in place rather than deleted, so a failed cross-filesystem move can't lose
data.

## Build: compiled from source at install time

syn-filemanager is **not** shipped as a prebuilt package on the ISO — it
is built from source during Stage 1 of installation, on the target
machine. See [Stage 1](../stage1.md) for the full installer sequence this
fits into.

The mechanics, confirmed directly in `syn-stage1.zsh`:

1. `syn-pacstrap.zsh` (Stage 0) deploys the `syn-filemanager-src/` source
   tree to `/usr/src/syn-filemanager` on the target's root filesystem —
   source only, no binary.
2. `syn-packages.zsh`'s `desktopStack` array installs `qt6-base` (runtime/
   link dependency) and `cmake` (build system) as part of the normal
   package set — both are present on the target well before Stage 1 runs,
   so the build step has everything it needs without a separate dependency
   pass. See [Packages](../packages.md).
3. Stage 1 (`syn-stage1.zsh`) `chown`s `/usr/src/syn-filemanager` to the
   just-created user account, then runs `makepkg -f --noconfirm` **as that
   user** (`su "$UserAccountName" -c '...'`) — `makepkg` refuses to run as
   root, and by this point in Stage 1 the real user account already
   exists. `PKGBUILD`'s `build()` step runs `cmake -B <build-dir> -S
   <project-dir> -DCMAKE_BUILD_TYPE=Release` then `cmake --build`; its
   `package()` step runs `cmake --install --prefix /usr` into a
   `pkgdir` staging tree, which `makepkg` then archives into a real
   `.pkg.tar.zst`.
4. Stage 1 installs the freshly-built package with `pacman -U
   /usr/src/syn-filemanager/syn-filemanager-*.pkg.tar.zst --noconfirm`,
   then removes the source tree.
5. **Recoverable-default behavior on build failure**: if the build or
   install step fails, Stage 1 does not abort the install. It logs an
   error explaining that the file browser (Super+E) won't work until
   syn-filemanager is built manually, and leaves `/usr/src/syn-filemanager`
   in place (rather than deleting it along with the evidence of what
   failed) so a manual `cd /usr/src/syn-filemanager && makepkg -f` retry is
   still possible after install. This follows the same recoverable-default
   philosophy documented in [Philosophy](../philosophy.md) — a build
   failure here degrades one feature, not the whole install.

The `PKGBUILD`'s own comment explains one more mechanical detail:
`makepkg` always creates a `$srcdir` directory named `src` next to the
`PKGBUILD` file it's given, regardless of whether `source=()` is empty —
which would collide with this project's own `src/` directory if `cmake`
were ever pointed at `$startdir`. To avoid that collision, every path in
the `PKGBUILD` is anchored on the `PKGBUILD`'s own real location (captured
once as `_projdir` via `$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)`)
rather than relying on the deprecated `$startdir` variable.

## CMake / package summary

- `CMakeLists.txt`: C++17, `CMAKE_AUTOMOC ON` (required for Qt's
  signal/slot meta-object system), links `Qt6::Widgets` only — no other Qt
  module. Installs the binary to `bin` and the `.desktop` file to
  `share/applications`.
- `PKGBUILD`: `pkgname=syn-filemanager`, depends on `qt6-base`, build-time
  dependency `cmake`. No `source=()` entries — it builds from the sources
  sitting next to the `PKGBUILD` itself, not a downloaded tarball.
- `resources/syn-filemanager.desktop`: `Exec=syn-filemanager %f`,
  `Icon=system-file-manager`, categories `FileManager;Utility;Qt;`.
