/* ------------------------------------------------------------------------
 *   mkarchiso bind-mounts host filesystems (sysfs, etc.) into
 *   subdirectories of the scratch dir while building. A killed or crashed run can
 *   leave those mounted, which then makes a plain rm -rf on the scratch
 *   dir fail with "Read-only file system" — this scans /proc/self/mounts
 *   for anything still mounted under a given prefix and unmounts it
 *   (deepest paths first, so a parent unmount never fails because a
 *   child is still mounted inside it), lazy-unmount (MNT_DETACH) as a
 *   fallback for anything still busy. Direct C port of
 *   BUILD-ARCHISO.zsh's own `mount | awk ... | sort -r | umount` logic.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MOUNT_CLEANUP_H
#define SYN_MOUNT_CLEANUP_H

/* Unmounts every entry in /proc/self/mounts whose mount point starts
 * with `prefix`, deepest-first. Best-effort: a mount that resists both
 * a normal and a lazy unmount is left in place (logged via the same
 * on_line callback style the rest of this tool uses, printf'd directly
 * here since this runs before/after the streamed-output phase, not
 * during it) rather than aborting the whole cleanup. */
void syn_mount_cleanup(const char *prefix);

#endif
