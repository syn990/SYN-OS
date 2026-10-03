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
#include "Jobs.h"
#include "JobsWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
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
                });
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
  case UndoStep::Renamed: {
    const FileJobs::Pair &p = step.pairs.first();
    if (QFileInfo::exists(p.from) || !QDir().rename(p.to, p.from)) {
      flash(tr("can't undo the %1: %2 is in the way or gone").arg(what, QFileInfo(p.from).fileName()), true);
      return;
    }
    m_pendingPath = p.from;
    tryPending();
    flash(tr("undid the %1").arg(what));
    break;
  }
  }
}
