// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, file-job half: paste, trash, delete, restore and undo, all
//   run as Jobs so the window never waits on the disk, and the JOBS
//   segment and JobsWindow that show them.
//
//   Delete moves to the trash straight away, no question: Ctrl+Z brings it
//   back. Shift+Delete (and anything already in the trash) deletes for
//   good, after asking. Undo covers the trash, moves, copies (the copies
//   go to the trash), renames and anything created (new folders and files,
//   extracted archives, packed ones).
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "FileJobs.h"
#include "FileOps.h"
#include "FileSortProxy.h"
#include "GitStatus.h"
#include "Jobs.h"
#include "JobsWindow.h"
#include "RowDelegate.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

namespace {

QString shortDir(const QString &dir)
{
  const QString name = QFileInfo(dir).fileName();
  return name.isEmpty() ? dir : name;
}

} // namespace

// ------------------------------------------------------------------- jobs

void MainWindow::buildJobs()
{
  m_jobs = new Jobs::Manager(this);
  m_jobsWindow = new JobsWindow(m_jobs, this);

  connect(m_jobs, &Jobs::Manager::tick, this, &MainWindow::updateJobButton);
  connect(m_jobs, &Jobs::Manager::jobsChanged, this, [this] {
    updateJobButton();
    // Open by itself once something has been going a moment: a quick
    // trash of one file shouldn't flash a window up.
    if (m_jobs->running() > 0 && !m_jobsWindow->isVisible())
      QTimer::singleShot(500, this, [this] {
        if (m_jobs->running() > 0 && !m_jobsWindow->isVisible()) {
          // Beside the work, not in front of it: the keyboard stays with
          // the files (Wayland may still focus it; take focus back).
          m_jobsAutoShown = true;
          QWidget *had = QApplication::focusWidget();
          m_jobsWindow->setAttribute(Qt::WA_ShowWithoutActivating, true);
          m_jobsWindow->show();
          m_jobsWindow->setAttribute(Qt::WA_ShowWithoutActivating, false);
          activateWindow();
          if (had)
            had->setFocus();
        }
      });
    // Opened by itself and everything went fine: out of the way again.
    if (m_jobs->running() == 0 && m_jobsAutoShown && m_jobsWindow->isVisible()) {
      bool failed = false;
      for (const Jobs::JobPtr &j : m_jobs->jobs())
        failed |= j->state == Jobs::State::Failed;
      if (!failed)
        QTimer::singleShot(2500, this, [this] {
          if (m_jobs->running() == 0 && m_jobsAutoShown) {
            m_jobsWindow->hide();
            m_jobsAutoShown = false;
          }
        });
    }
  });
}

void MainWindow::showJobs()
{
  m_jobsAutoShown = false; // asked for: stays until closed
  if (m_jobsWindow->isVisible()) {
    m_jobsWindow->hide();
    return;
  }
  m_jobsWindow->show();
  m_jobsWindow->raise();
  m_jobsWindow->activateWindow();
}

void MainWindow::updateJobButton()
{
  const int running = m_jobs->running();
  if (m_jobs->jobs().isEmpty()) {
    m_jobBtn->hide();
    return;
  }
  if (running) {
    const double f = m_jobs->overall();
    m_jobBtn->setText(f < 0 ? tr("JOBS %1").arg(running)
                            : tr("JOBS %1  %2%").arg(running).arg(int(f * 100)));
  } else {
    m_jobBtn->setText(tr("JOBS done"));
  }
  m_jobBtn->setChecked(running > 0);
  m_jobBtn->show();
}

void MainWindow::runFileJob(const QString &title, std::function<FileJobs::Pairs(Jobs::Progress &)> work,
                            std::function<void(const FileJobs::Pairs &, Jobs::Job &)> done)
{
  auto result = std::make_shared<FileJobs::Pairs>();
  m_jobs->start(title,
                [work, result](Jobs::Progress &p) { *result = work(p); },
                [this, result, done](Jobs::Job &job) {
                  done(*result, job);
                  const QStringList errors = job.progress->errors();
                  if (job.state == Jobs::State::Failed) {
                    FileOps::soundFailure();
                    flash(tr("%1, %n problem(s): %2", nullptr, int(errors.size()))
                            .arg(job.summary, errors.first()), true);
                  } else if (job.state == Jobs::State::Cancelled) {
                    flash(tr("cancelled: %1").arg(job.title));
                  } else {
                    if (job.elapsedMs > 3000)
                      FileOps::soundSuccess();
                    flash(job.summary);
                  }
                  updateDisk();
                  m_git->poke();
                });
}

// ------------------------------------------------------------ folder sizes

void MainWindow::measureFolders()
{
  if (refuseInArchive())
    return;
  QStringList folders;
  for (const QModelIndex &idx : m_view->selectionModel()->selectedIndexes())
    if (idx.parent() == m_view->rootIndex() && m_proxy->infoOf(idx).isDir())
      folders << m_proxy->pathOf(idx);
  if (folders.isEmpty())
    for (int r = 0; r < m_proxy->rowCount(m_view->rootIndex()); ++r) {
      const QModelIndex idx = m_proxy->index(r, 0, m_view->rootIndex());
      const QFileInfo fi = m_proxy->infoOf(idx);
      if (fi.isDir() && !fi.isSymLink())
        folders << fi.absoluteFilePath();
    }
  if (folders.isEmpty()) {
    flash(tr("no folders here to measure"));
    return;
  }
  // Without the field the sizes have nowhere to show: switch it on.
  if (!(RowDelegate::fields() & RowDelegate::Size))
    toggleField(RowDelegate::Size);

  struct Sizes { QHash<QString, qint64> bytes, items; };
  auto out = std::make_shared<Sizes>();
  const QString where = shortDir(m_currentDir);
  runFileJob(tr("Measuring %n folder(s) in %1", nullptr, int(folders.size())).arg(where),
             [folders, out](Jobs::Progress &p) {
               p.bytes = false;
               p.total = folders.size();
               for (const QString &f : folders) {
                 if (p.cancel)
                   break;
                 p.setCurrent(f);
                 qint64 items = 0;
                 const qint64 bytes = FileJobs::folderSize(p, f, &items);
                 if (!p.cancel) {
                   out->bytes.insert(f, bytes);
                   out->items.insert(f, items);
                 }
                 ++p.done;
               }
               return FileJobs::Pairs();
             },
             [this, out](const FileJobs::Pairs &, Jobs::Job &job) {
               qint64 total = 0;
               for (auto it = out->bytes.cbegin(); it != out->bytes.cend(); ++it) {
                 m_folderSizes.insert(it.key(), it.value());
                 m_folderItems.insert(it.key(), out->items.value(it.key()));
                 total += it.value();
               }
               job.summary = tr("%n folder(s) measured, %1 in all", nullptr, int(out->bytes.size()))
                               .arg(humanSize(total));
               m_view->viewport()->update();
               updateInfo();
             });
}

// ------------------------------------------------------------------- git

void MainWindow::updateGitSegment()
{
  if (!m_git->inRepo()) {
    m_gitSeg->hide();
    return;
  }
  // branch, ahead/behind, then staged / changed / new / conflicts
  QStringList parts{m_git->branch().isEmpty() ? tr("(no branch)") : m_git->branch()};
  if (m_git->ahead())
    parts << QStringLiteral("\u2191%1").arg(m_git->ahead());
  if (m_git->behind())
    parts << QStringLiteral("\u2193%1").arg(m_git->behind());
  QStringList counts;
  if (m_git->staged())
    counts << QStringLiteral("+%1").arg(m_git->staged());
  if (m_git->changed())
    counts << QStringLiteral("~%1").arg(m_git->changed());
  if (m_git->untracked())
    counts << QStringLiteral("?%1").arg(m_git->untracked());
  if (m_git->conflicts())
    counts << QStringLiteral("!%1").arg(m_git->conflicts());
  m_gitSeg->setText(parts.join(QLatin1Char(' ')) + (counts.isEmpty() ? QString() : QStringLiteral("  ") + counts.join(QLatin1Char(' '))));
  m_gitSeg->setToolTip(tr("%1\n%2 staged, %3 changed, %4 new, %5 in conflict\n%6 ahead, %7 behind")
                         .arg(m_git->root()).arg(m_git->staged()).arg(m_git->changed())
                         .arg(m_git->untracked()).arg(m_git->conflicts())
                         .arg(m_git->ahead()).arg(m_git->behind()));
  m_gitSeg->show();
}

QString MainWindow::gitWords(const QString &path) const
{
  const GitStatus::Mark m = m_git->markFor(path);
  if (!m.any())
    return QString();
  if (m.ignored)
    return tr("git: ignored");
  if (m.untracked)
    return tr("git: new");
  if (m.x == 'U' || m.y == 'U' || (m.x == m.y && (m.x == 'A' || m.x == 'D')))
    return tr("git: in conflict");
  QStringList w;
  auto word = [](char c) {
    switch (c) {
    case 'M': return QObject::tr("modified");
    case 'A': return QObject::tr("added");
    case 'D': return QObject::tr("deleted");
    case 'R': return QObject::tr("renamed");
    case 'C': return QObject::tr("copied");
    case 'U': return QObject::tr("in conflict");
    case 'T': return QObject::tr("type changed");
    default: return QString();
    }
  };
  if (m.x != ' ')
    w << word(m.x) + tr(", staged");
  if (m.y != ' ')
    w << word(m.y);
  if (w.isEmpty() && m.dirty)
    w << tr("changes inside");
  return QStringLiteral("git: ") + w.join(QStringLiteral("; "));
}

// ------------------------------------------------------------------ paste

void MainWindow::paste()
{
  if (refuseInArchive())
    return;
  const QMimeData *mime = QApplication::clipboard()->mimeData();
  QStringList paths;
  if (mime)
    for (const QUrl &url : mime->urls())
      if (url.isLocalFile())
        paths << url.toLocalFile();
  if (paths.isEmpty()) {
    flash(tr("nothing to paste"), true);
    return;
  }
  const bool cut = mime->data(QByteArrayLiteral("x-special/gnome-copied-files")).startsWith("cut");
  const QString dest = m_currentDir;
  const std::optional<FileJobs::Conflict> policy = FileOps::askTransfer(this, paths, dest, cut);
  if (!policy)
    return;
  if (cut)
    QApplication::clipboard()->clear(); // a cut pastes once

  const QString title = cut ? tr("Moving %n item(s) to %1", nullptr, int(paths.size())).arg(shortDir(dest))
                            : tr("Copying %n item(s) to %1", nullptr, int(paths.size())).arg(shortDir(dest));
  runFileJob(title,
             [paths, dest, cut, policy](Jobs::Progress &p) {
               return cut ? FileJobs::move(p, paths, dest, *policy)
                          : FileJobs::copy(p, paths, dest, *policy);
             },
             [this, cut, dest](const FileJobs::Pairs &done, Jobs::Job &job) {
               job.summary = cut ? tr("%n moved to %1", nullptr, int(done.size())).arg(shortDir(dest))
                                 : tr("%n copied to %1", nullptr, int(done.size())).arg(shortDir(dest));
               if (done.isEmpty())
                 return;
               pushUndo(cut ? UndoStep::Moved : UndoStep::Copied, done,
                        cut ? tr("move of %n item(s)", nullptr, int(done.size()))
                            : tr("copy of %n item(s)", nullptr, int(done.size())));
               if (!m_inArchive && dest == m_currentDir) {
                 m_pendingPath = done.first().to;
                 tryPending();
               }
             });
}

// ------------------------------------------------------------ trash, delete

bool MainWindow::inTrashView() const
{
  return !m_inArchive && FileJobs::isInTrash(m_currentDir + QStringLiteral("/x"));
}

void MainWindow::deleteTargets()
{
  if (refuseInArchive())
    return;
  if (inTrashView()) {
    deletePermanently(); // already in the trash: the only way on is out
    return;
  }
  const QStringList paths = targets();
  if (paths.isEmpty())
    return;
  clearMarks();
  runFileJob(tr("Moving %n item(s) to the trash", nullptr, int(paths.size())),
             [paths](Jobs::Progress &p) { return FileJobs::trash(p, paths); },
             [this](const FileJobs::Pairs &done, Jobs::Job &job) {
               job.summary = tr("%n moved to the trash  (Ctrl+Z undoes)", nullptr, int(done.size()));
               if (!done.isEmpty())
                 pushUndo(UndoStep::Trashed, done, tr("trash of %n item(s)", nullptr, int(done.size())));
             });
}

void MainWindow::deletePermanently()
{
  if (refuseInArchive())
    return;
  const QStringList paths = targets();
  if (paths.isEmpty() || !FileOps::confirmPermanentDelete(this, paths))
    return;
  clearMarks();
  runFileJob(tr("Deleting %n item(s)", nullptr, int(paths.size())),
             [paths](Jobs::Progress &p) {
               // Their .trashinfo goes with them when deleting from the trash.
               FileJobs::Pairs out = FileJobs::remove(p, paths);
               for (const QString &path : paths) {
                 if (!FileJobs::isInTrash(path))
                   continue;
                 const QFileInfo fi(path);
                 QFile::remove(QFileInfo(fi.absolutePath()).absolutePath() + QStringLiteral("/info/")
                               + fi.fileName() + QStringLiteral(".trashinfo"));
               }
               return out;
             },
             [this](const FileJobs::Pairs &done, Jobs::Job &job) {
               job.summary = tr("%n deleted", nullptr, int(done.size()));
             });
}

void MainWindow::restoreTargets()
{
  const QStringList paths = targets();
  if (paths.isEmpty() || !inTrashView())
    return;
  clearMarks();
  runFileJob(tr("Restoring %n item(s)", nullptr, int(paths.size())),
             [paths](Jobs::Progress &p) { return FileJobs::restore(p, paths); },
             [this](const FileJobs::Pairs &done, Jobs::Job &job) {
               job.summary = tr("%n restored", nullptr, int(done.size()));
               if (done.size() == 1)
                 job.summary += QStringLiteral(" to ") + QFileInfo(done.first().to).absolutePath();
             });
}

void MainWindow::emptyTrash()
{
  if (QMessageBox::question(this, tr("Empty the trash"),
                            tr("Delete everything in the trash for good?"),
                            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
      != QMessageBox::Yes)
    return;
  runFileJob(tr("Emptying the trash"),
             [](Jobs::Progress &p) { FileJobs::emptyTrash(p); return FileJobs::Pairs(); },
             [](const FileJobs::Pairs &, Jobs::Job &job) { job.summary = tr("trash emptied"); });
}

void MainWindow::openTrash()
{
  const QString files = FileJobs::homeTrash() + QStringLiteral("/files");
  QDir().mkpath(files);
  navigateTo(files);
}

// ------------------------------------------------------------------- undo

void MainWindow::pushUndo(UndoStep::Kind kind, const FileJobs::Pairs &pairs, const QString &label)
{
  m_undo << UndoStep{kind, pairs, label};
  if (m_undo.size() > 50)
    m_undo.removeFirst();
}

void MainWindow::undo()
{
  if (m_undo.isEmpty()) {
    flash(tr("nothing to undo"));
    return;
  }
  const UndoStep step = m_undo.takeLast();
  const QString what = step.label;
  auto finish = [this, what, step](const FileJobs::Pairs &done, Jobs::Job &job) {
    if (done.isEmpty() && job.state != Jobs::State::Done) {
      m_undo << step; // nothing came back: still there to try again
      job.summary = tr("couldn't undo the %1").arg(what);
      return;
    }
    job.summary = tr("undid the %1").arg(what);
    if (!done.isEmpty() && QFileInfo(done.first().to).absolutePath() == m_currentDir) {
      m_pendingPath = done.first().to;
      tryPending();
    }
  };

  switch (step.kind) {
  case UndoStep::Trashed: {
    QStringList trashed;
    for (const FileJobs::Pair &p : step.pairs)
      trashed << p.to;
    runFileJob(tr("Undo: restoring %n item(s)", nullptr, int(trashed.size())),
               [trashed](Jobs::Progress &p) { return FileJobs::restore(p, trashed); }, finish);
    break;
  }
  case UndoStep::Moved:
    runFileJob(tr("Undo: moving %n item(s) back", nullptr, int(step.pairs.size())),
               [pairs = step.pairs](Jobs::Progress &p) { return FileJobs::moveBack(p, pairs); }, finish);
    break;
  case UndoStep::Copied:
  case UndoStep::Created: {
    // What was made goes to the trash, not away for good.
    QStringList made;
    for (const FileJobs::Pair &p : step.pairs)
      made << (step.kind == UndoStep::Copied ? p.to : p.from);
    runFileJob(tr("Undo: %n item(s) to the trash", nullptr, int(made.size())),
               [made](Jobs::Progress &p) { return FileJobs::trash(p, made); },
               [this, what, step](const FileJobs::Pairs &done, Jobs::Job &job) {
                 if (done.isEmpty() && job.state != Jobs::State::Done) {
                   m_undo << step;
                   job.summary = tr("couldn't undo the %1").arg(what);
                   return;
                 }
                 job.summary = tr("undid the %1 (it's in the trash)").arg(what);
               });
    break;
  }
  case UndoStep::Chmoded:
    undoModes(step.pairs);
    flash(tr("undid the %1").arg(what));
    break;
  case UndoStep::Renamed: {
    FileJobs::Pairs back;
    for (const FileJobs::Pair &p : step.pairs)
      back << FileJobs::Pair{p.to, p.from};
    const QString error = renameAll(back);
    if (!error.isEmpty()) {
      m_undo << step;
      flash(tr("can't undo the %1: %2").arg(what, error), true);
      return;
    }
    m_pendingPath = back.first().to;
    tryPending();
    flash(tr("undid the %1").arg(what));
    break;
  }
  }
}
