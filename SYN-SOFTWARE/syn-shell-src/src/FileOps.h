// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   FileOps: the questions asked before a file job starts, on the UI
//   thread, so FileJobs (the work, on a worker) never has to stop and ask.
//   Pasting a folder into itself or moving something onto itself is
//   refused here; names that already exist at the destination get one
//   question for the whole paste: replace, keep both, or skip. Also the
//   one place the job results sound through syn-bar-core.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include "FileJobs.h"

#include <QString>
#include <QStringList>
#include <QWidget>

#include <optional>

namespace FileOps {

// What to do with a paste, or nothing if it shouldn't happen.
std::optional<FileJobs::Conflict> askTransfer(QWidget *parent, const QStringList &paths,
                                              const QString &destDir, bool move);

// Deleting for good (not to the trash): asks first.
bool confirmPermanentDelete(QWidget *parent, const QStringList &paths);

// The job tones: SUCCESS when a long one finishes, FAIL when one fails.
void soundSuccess();
void soundFailure();

} // namespace FileOps
