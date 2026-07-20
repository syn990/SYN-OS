// ------------------------------------------------------------------------------
//                     S Y N - F I L E M A N A G E R
//
//   FileOps: delete/copy/move logic and their confirmation dialogs, kept
//   out of MainWindow so filesystem-error handling isn't tangled into the
//   UI class. Every entry point here shows its own QMessageBox — callers
//   don't need to know whether an operation succeeded to decide whether
//   to warn the user, that's handled internally.
//
//   Symlinks: copy recreates the link itself (readlink + create a new
//   symlink at the destination) rather than dereferencing it — copying a
//   broken or self-referential symlink's target would either fail or
//   loop, and recreating the link matches what most users expect from
//   "copy" on a shortcut. Delete removes the link, never its target.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-FILEMANAGER (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

namespace FileOps {

// Prompts for confirmation, then permanently deletes every path in
// `paths` (files and/or directories). No trash — this is final. Shows a
// summary dialog if any entry fails partway through the batch.
void deleteEntries(QWidget *parent, const QStringList &paths);

// Copies each path in `paths` into `destDir`. Recurses into directories.
// Prompts once up front if any destination would overwrite an existing
// entry, then proceeds best-effort — a failure on one entry doesn't stop
// the rest. Shows a summary dialog listing any failures.
void copyEntries(QWidget *parent, const QStringList &paths, const QString &destDir);

// Moves each path in `paths` into `destDir`. Tries a same-filesystem
// rename first; falls back to copy-then-delete-source automatically when
// the rename fails because source and destination are on different
// filesystems (EXDEV) — e.g. moving to a different mount or a USB drive.
void moveEntries(QWidget *parent, const QStringList &paths, const QString &destDir);

} // namespace FileOps
