// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   FileJobs: the file operations themselves, as blocking calls run on a
//   Jobs worker. Copy goes in 1 MiB chunks so progress is real and cancel
//   stops mid-file (the half-written file is removed); permissions and
//   modification times come along; symlinks are recreated, never
//   followed. A move is a rename when it can be and a copy-then-delete
//   across filesystems, the original removed only once its copy is whole.
//
//   Trash is the freedesktop.org one every other app uses: files/ and
//   info/*.trashinfo under ~/.local/share/Trash, or .Trash-UID at the top
//   of another drive, so nothing has to cross filesystems to be binned.
//
//   Every call returns what went where (Pair from -> to), which is what
//   undo and the cursor afterwards are built from.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include "Jobs.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace FileJobs {

// What to do when the destination already has that name.
enum class Conflict { Replace, KeepBoth, Skip };

struct Pair
{
  QString from;
  QString to;
};
using Pairs = QList<Pair>;

Pairs copy(Jobs::Progress &p, const QStringList &sources, const QString &destDir, Conflict policy);
Pairs move(Jobs::Progress &p, const QStringList &sources, const QString &destDir, Conflict policy);
// Puts each `to` back at its exact `from` (undoing a move).
Pairs moveBack(Jobs::Progress &p, const Pairs &moved);
Pairs remove(Jobs::Progress &p, const QStringList &paths); // permanent
Pairs trash(Jobs::Progress &p, const QStringList &paths);  // from = original, to = in the trash
// Out of the trash to where each came from (a free name if that's taken).
Pairs restore(Jobs::Progress &p, const QStringList &trashed);
void emptyTrash(Jobs::Progress &p);

// Bytes (apparent size, as ls shows) and entries under a folder, links
// not followed; polls cancel.
qint64 folderSize(Jobs::Progress &p, const QString &path, qint64 *items);

QString homeTrash();        // ~/.local/share/Trash
bool isInTrash(const QString &path);
QString originalPath(const QString &trashedFile); // from its .trashinfo, "" if unknown
// "notes.txt" -> "notes (2).txt" until nothing has that name.
QString freeName(const QString &path);

} // namespace FileJobs
