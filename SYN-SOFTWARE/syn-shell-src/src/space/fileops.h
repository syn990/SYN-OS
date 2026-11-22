/* ------------------------------------------------------------------------
 *   What the list's right-click menu does to the files themselves: copy
 *   and move into a folder, rename, a new folder, and Delete, which moves
 *   to the freedesktop Trash and never deletes for good. Plain C, no
 *   graphics. Each returns false with why in err. Delete permanently
 *   (fo_remove) is the one action that cannot be undone.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_FILEOPS_H
#define SYN_SHELL_SPACE_FILEOPS_H

#include <stdbool.h>

/* Whether name will do as one path part: not empty, no '/', not . or .. */
bool fo_name_ok(const char *name, char *err, int errlen);

/* Renames path within its folder; never over something already there.
 * The new whole path goes in out. */
bool fo_rename(const char *path, const char *name, char *out, int outlen, char *err, int errlen);

/* Makes dir/name; the new whole path goes in out. */
bool fo_mkdir(const char *dir, const char *name, char *out, int outlen, char *err, int errlen);

/* Copies (move false) or moves src into the folder dir, under its own
 * name, or "NAME (copy)", "NAME (copy 2)" ... where that is taken. A move
 * across file systems is a copy and then the original's removal, as mv
 * does. The new whole path goes in out. Slow for big things: run it off
 * the window's thread. */
bool fo_paste(const char *src, const char *dir, bool move, char *out, int outlen, char *err, int errlen);

/* Moves path to the Trash: ~/.local/share/Trash where path is on the
 * home's file system, otherwise the .Trash-UID folder at the top of its
 * own, with the .trashinfo a file manager restores it from. */
bool fo_trash(const char *path, char *err, int errlen);

/* Deletes path for good: a file, a link (never what it points at), or a
 * folder with everything under it. Never crosses into another file system
 * mounted inside the folder. Stops at the first thing that will not go,
 * with what and why in err; what went before it is gone. */
bool fo_remove(const char *path, char *err, int errlen);

#endif
