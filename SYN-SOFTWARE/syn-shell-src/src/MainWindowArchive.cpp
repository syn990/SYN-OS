// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, archive half: walking into archives, previewing them and
//   their entries, extracting, copying out and compressing. The disk
//   half (MainWindow.cpp) calls in here wherever a location can be an
//   archive. libarchive work always runs on the global thread pool:
//   foreground jobs through startJob() (one at a time, progress in the
//   header, Esc cancels), previews on their own generation counter so a
//   stale result never paints over a newer cursor.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "Archive.h"
#include "ArchiveModel.h"
#include "FileSortProxy.h"
#include "Preview.h"
#include "RowDelegate.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMap>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPointer>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>

namespace {

QString modeString(uint mode, bool dir, bool link)
{
  QString s(link ? QLatin1Char('l') : dir ? QLatin1Char('d') : QLatin1Char('-'));
  if ((mode & 07777) == 0)
    return s + QStringLiteral("?????????"); // a folder the archive only implies
  const char rwx[] = "rwxrwxrwx";
  for (int i = 0; i < 9; ++i)
    s += (mode & (0400 >> i)) ? QLatin1Char(rwx[i]) : QLatin1Char('-');
  return s;
}

QString uniquePath(const QString &path)
{
  QString candidate = path;
  for (int i = 2; QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink(); ++i)
    candidate = path + QLatin1Char('-') + QString::number(i);
  return candidate;
}

// What the preview column says about an archive the cursor is on.
QString archiveSummary(const Archive::Listing &l)
{
  if (l.needPassword)
    return QObject::tr("encrypted, even the file list\n\n"
                       "l   asks for the password and walks in\n"
                       "r   opens it in the default app");
  if (l.entries.isEmpty() && !l.error.isEmpty())
    return QObject::tr("can't read it: %1").arg(l.error);

  // Every path plus the folders they imply, sorted so a folder's
  // contents follow it ('/' sorts before '-', which would split them).
  struct Row { bool dir; qint64 size; };
  QMap<QString, Row> rows;
  int files = 0;
  qint64 unpacked = 0;
  for (const Archive::Entry &e : l.entries) {
    QString key = e.path;
    key.replace(QLatin1Char('/'), QChar(1));
    rows.insert(key, {e.dir, e.size});
    for (int i = key.indexOf(QChar(1)); i >= 0; i = key.indexOf(QChar(1), i + 1))
      if (!rows.contains(key.left(i)))
        rows.insert(key.left(i), {true, -1});
    if (!e.dir) {
      ++files;
      unpacked += qMax<qint64>(0, e.size);
    }
  }
  const int folders = int(std::count_if(rows.cbegin(), rows.cend(), [](const Row &r) { return r.dir; }));

  QString out;
  out += QStringLiteral("FORMAT     ") + l.format;
  if (!l.filters.isEmpty())
    out += QStringLiteral("  +  ") + l.filters.join(QStringLiteral(" + "));
  out += QStringLiteral("\nENTRIES    %1 files  %2 folders").arg(files).arg(folders);
  if (!l.complete)
    out += QObject::tr("  (the first %1 read; l reads them all)").arg(l.entries.size());
  out += QLatin1Char('\n');
  if (l.complete)
    out += QStringLiteral("UNPACKED   %1\n").arg(humanSize(unpacked));
  if (l.encrypted)
    out += QObject::tr("ENCRYPTED  yes, l asks for the password\n");
  if (l.unsafe)
    out += QObject::tr("UNSAFE     %1 entries with \"..\" in the path, never extracted\n").arg(l.unsafe);
  if (!l.error.isEmpty())
    out += QStringLiteral("ERROR      ") + l.error + QLatin1Char('\n');
  out += QObject::tr("\nl browse    X extract here    r default app\n\n");

  int shown = 0;
  for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
    if (++shown > 600) {
      out += QObject::tr("  ... %1 more\n").arg(rows.size() - 600);
      break;
    }
    const int depth = int(it.key().count(QChar(1)));
    const QString name = it.key().section(QChar(1), -1)
                       + (it.value().dir ? QStringLiteral("/") : QString());
    QString line = QString(depth * 2, QLatin1Char(' ')) + name;
    if (!it.value().dir)
      line = line.leftJustified(58, QLatin1Char(' '))
           + (it.value().size >= 0 ? humanSize(it.value().size) : QStringLiteral("?"));
    out += line + QLatin1Char('\n');
  }
  return out;
}

} // namespace

MainWindow::~MainWindow() = default;

// ------------------------------------------------------------------- jobs

bool MainWindow::startJob(
  const QString &label,
  std::function<void(const std::atomic_bool *, const std::function<void(double)> &)> work,
  std::function<void()> done)
{
  if (m_jobCancel) {
    flash(tr("busy %1, Esc cancels it").arg(m_jobLabel), true);
    return false;
  }
  auto cancel = std::make_shared<std::atomic_bool>(false);
  m_jobCancel = cancel;
  m_jobLabel = label;
  m_jobSeg->setText(label);
  m_jobSeg->show();

  QPointer<MainWindow> self(this);
  const std::function<void(double)> progress = [self, cancel, label](double f) {
    QMetaObject::invokeMethod(qApp, [self, cancel, label, f] {
      if (self && self->m_jobCancel == cancel)
        self->m_jobSeg->setText(QStringLiteral("%1 %2%").arg(label).arg(int(f * 100)));
    }, Qt::QueuedConnection);
  };
  QThreadPool::globalInstance()->start([self, cancel, work, done, progress] {
    work(cancel.get(), progress);
    QMetaObject::invokeMethod(qApp, [self, cancel, done] {
      if (!self || self->m_jobCancel != cancel)
        return;
      self->m_jobCancel.reset();
      self->m_jobSeg->hide();
      done();
    }, Qt::QueuedConnection);
  });
  return true;
}

void MainWindow::cancelPreview()
{
  if (m_previewCancel)
    *m_previewCancel = true;
  m_previewCancel.reset();
  ++m_previewGen;
}

QString MainWindow::askPassword(const QString &file, bool retry)
{
  bool ok = false;
  const QString name = QFileInfo(file).fileName();
  const QString pw = QInputDialog::getText(
    this, tr("Password"),
    retry ? tr("Wrong password for %1. Try again:").arg(name)
          : tr("%1 is encrypted. Password:").arg(name),
    QLineEdit::Password, QString(), &ok);
  return ok ? pw : QString();
}

QString MainWindow::tempSlot()
{
  if (!m_tempDir)
    m_tempDir = std::make_unique<QTemporaryDir>(QDir::tempPath()
                                                + QStringLiteral("/syn-shell-XXXXXX"));
  const QString slot = m_tempDir->path() + QLatin1Char('/') + QString::number(++m_tempSlots);
  QDir().mkpath(slot);
  return slot;
}

// ---------------------------------------------------------------- walking

void MainWindow::openArchive(const QString &file, const QString &inner, bool recordHistory,
                             const QString &password)
{
  if (m_session && m_session->file() == file && !m_session->isStale()) {
    enterArchiveDir(m_session->isDir(inner) ? inner : QString(), recordHistory);
    return;
  }

  auto result = std::make_shared<Archive::Listing>();
  startJob(tr("reading"),
           [file, password, result](const std::atomic_bool *cancel,
                                    const std::function<void(double)> &progress) {
             *result = Archive::list(file, password, 0, 0, cancel, progress);
           },
           [this, file, inner, recordHistory, password, result] {
             const QString name = QFileInfo(file).fileName();
             if (result->cancelled) {
               flash(tr("cancelled"));
               return;
             }
             if (result->needPassword) {
               const QString pw = askPassword(file, !password.isEmpty());
               if (!pw.isEmpty())
                 openArchive(file, inner, recordHistory, pw);
               return;
             }
             if (result->entries.isEmpty() && !result->error.isEmpty()) {
               flash(tr("can't read %1: %2").arg(name, result->error), true);
               return;
             }
             if (!result->error.isEmpty())
               flash(tr("%1 is damaged, showing what could be read: %2").arg(name, result->error),
                     true);

             // The views must let go of the old session's model before
             // it's replaced.
             bool left = false;
             if (m_inArchive) {
               leaveLocation(recordHistory);
               leaveArchive();
               left = true;
             }
             m_session = std::make_unique<ArchiveSession>(file, *result, password);
             m_session->proxy()->setShowHidden(m_model->filter() & QDir::Hidden);
             enterArchiveDir(m_session->isDir(inner) ? inner : QString(), recordHistory, !left);
           });
}

void MainWindow::enterArchiveDir(const QString &inner, bool recordHistory, bool leave)
{
  if (!m_session)
    return;
  if (leave)
    leaveLocation(recordHistory);
  cancelPreview();
  if (!m_inArchive) {
    m_inArchive = true;
    m_currentDir = QFileInfo(m_session->file()).absolutePath();
    m_model->setRootPath(m_currentDir);
    setViewModel(m_view, m_session->proxy());
  }
  m_view->selectionModel()->clear();
  m_arcDir = inner;
  m_lastRow = 0;
  m_cursorAuto = true;
  m_view->setRootIndex(m_session->indexOf(inner));

  // Parent column: the folder holding the archive at its top level,
  // otherwise the archive folder above this one.
  QModelIndex here;
  if (inner.isEmpty()) {
    setViewModel(m_parentView, m_proxy);
    here = m_proxy->indexOf(m_session->file());
  } else {
    setViewModel(m_parentView, m_session->proxy());
    here = m_session->indexOf(inner);
  }
  m_parentView->setRootIndex(here.parent());
  m_parentView->selectionModel()->setCurrentIndex(here, QItemSelectionModel::NoUpdate);
  m_parentView->scrollTo(here, QAbstractItemView::PositionAtCenter);

  rebuildCrumbs();
  updateDisk();
  updateHistoryButtons();
  syncShell(); // the shell waits in the folder holding the archive
  setWindowTitle(QStringLiteral("syn-shell: ") + QFileInfo(m_session->file()).fileName()
                 + (inner.isEmpty() ? QString() : QLatin1Char('/') + inner));

  m_pendingPath = m_lastCursor.value(location());
  m_pendingEdit = false;
  tryPending();
  updateInfo();
  m_previewTimer->start();
}

void MainWindow::leaveArchive()
{
  cancelPreview();
  m_inArchive = false;
  m_arcDir.clear();
  setViewModel(m_view, m_proxy);
  setViewModel(m_parentView, m_proxy);
}

QStringList MainWindow::archiveTargets() const
{
  QStringList entries;
  if (!m_inArchive)
    return entries;
  for (const QModelIndex &idx : m_view->selectionModel()->selectedIndexes())
    if (idx.parent() == m_view->rootIndex())
      entries << ArchiveSession::pathOf(idx);
  if (entries.isEmpty() && cursor().isValid())
    entries << ArchiveSession::pathOf(cursor());
  return entries;
}

bool MainWindow::refuseInArchive()
{
  if (!m_inArchive)
    return false;
  flash(tr("archives are read-only here: X extracts, yy copies out"), true);
  return true;
}

// --------------------------------------------------------------- extract

void MainWindow::extractHere()
{
  if (m_inArchive) {
    QStringList entries = archiveTargets();
    if (entries.isEmpty()) {
      // Nothing under the cursor: an empty folder in the archive, or its
      // top level with nothing selected. Take the whole folder.
      if (m_arcDir.isEmpty())
        return;
      entries << m_arcDir;
    }
    QStringList clash;
    for (const QString &e : entries) {
      const QString name = e.section(QLatin1Char('/'), -1);
      if (QFileInfo::exists(m_currentDir + QLatin1Char('/') + name))
        clash << name;
    }
    if (!clash.isEmpty()
        && QMessageBox::question(this, tr("Extract"),
                                 tr("Already beside the archive, will be replaced:\n\n%1")
                                   .arg(clash.join(QLatin1Char('\n'))),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
             != QMessageBox::Yes)
      return;
    clearMarks();
    extractEntries(entries, m_currentDir, tr("extracting"), [this](const QStringList &paths) {
      flash(tr("%n extracted beside the archive", nullptr, paths.size()));
    });
    return;
  }

  const QModelIndex c = cursor();
  if (!c.isValid())
    return;
  const QString path = m_proxy->pathOf(c);
  if (QFileInfo(path).isDir()) {
    flash(tr("X extracts archives, this is a folder"), true);
    return;
  }
  extractArchiveFile(path, QString(), QString());
}

// Extract a whole archive file. Everything goes into a hidden scratch
// folder beside it first, then: a single top-level entry that doesn't
// clash moves out on its own (no "photos/photos/" nesting), anything
// else becomes a folder named after the archive. Both are renames on one
// filesystem, so nothing half-extracted ever appears under a real name.
void MainWindow::extractArchiveFile(const QString &file, const QString &dest,
                                    const QString &password)
{
  const QString folder = dest.isEmpty() ? QFileInfo(file).absolutePath() : dest;
  QString pw = password;
  if (pw.isEmpty() && m_session && m_session->file() == file)
    pw = m_session->password();
  const QString base = Archive::stripArchiveSuffix(QFileInfo(file).fileName());

  auto result = std::make_shared<Archive::Written>();
  auto target = std::make_shared<QString>();
  startJob(tr("extracting"),
           [file, folder, pw, base, result, target](const std::atomic_bool *cancel,
                                                    const std::function<void(double)> &progress) {
             QDir().mkpath(folder);
             QTemporaryDir scratch(folder + QStringLiteral("/.syn-extract-XXXXXX"));
             if (!scratch.isValid()) {
               result->error = QObject::tr("can't create a folder in %1").arg(folder);
               return;
             }
             *result = Archive::extract(file, pw, {}, QString(), scratch.path(), cancel, progress);
             if (!result->ok())
               return; // scratch removes itself
             const QStringList top = QDir(scratch.path()).entryList(
               QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
             const QString single = top.size() == 1 ? folder + QLatin1Char('/') + top.first()
                                                    : QString();
             if (!single.isEmpty() && !QFileInfo::exists(single) && !QFileInfo(single).isSymLink()) {
               if (QDir().rename(scratch.path() + QLatin1Char('/') + top.first(), single))
                 *target = single;
             } else {
               const QString named = uniquePath(folder + QLatin1Char('/') + base);
               if (QDir().rename(scratch.path(), named)) {
                 scratch.setAutoRemove(false);
                 *target = named;
               }
             }
             if (target->isEmpty())
               result->error = QObject::tr("extracted, but couldn't move it out of %1")
                                 .arg(scratch.path());
           },
           [this, file, dest, pw, result, target] {
             if (result->needPassword) {
               const QString again = askPassword(file, !pw.isEmpty());
               if (!again.isEmpty())
                 extractArchiveFile(file, dest, again);
               return;
             }
             if (result->cancelled) {
               flash(tr("cancelled, nothing extracted"));
               return;
             }
             if (!result->error.isEmpty()) {
               flash(result->error, true);
               return;
             }
             flash(tr("extracted to %1%2").arg(QFileInfo(*target).fileName(),
                                               result->skipped
                                                 ? tr("  (%n skipped)", nullptr, result->skipped)
                                                 : QString()));
             if (!m_inArchive && QFileInfo(*target).absolutePath() == m_currentDir) {
               m_pendingPath = *target;
               tryPending();
             }
             updateDisk();
           });
}

void MainWindow::extractEntries(const QStringList &entries, const QString &dest,
                                const QString &verb, std::function<void(const QStringList &)> done)
{
  if (!m_session || entries.isEmpty())
    return;
  const QString file = m_session->file();
  const QString password = m_session->password();
  const QString strip = m_arcDir;

  auto result = std::make_shared<Archive::Written>();
  startJob(verb,
           [file, password, entries, strip, dest, result](const std::atomic_bool *cancel,
                                                          const std::function<void(double)> &progress) {
             QDir().mkpath(dest);
             *result = Archive::extract(file, password, entries, strip, dest, cancel, progress);
           },
           [this, file, password, entries, dest, verb, done, result] {
             if (result->needPassword) {
               const QString pw = askPassword(file, !password.isEmpty());
               if (pw.isEmpty() || !m_session || m_session->file() != file)
                 return;
               m_session->setPassword(pw);
               extractEntries(entries, dest, verb, done);
               return;
             }
             if (result->cancelled) {
               flash(tr("cancelled"));
               return;
             }
             if (!result->error.isEmpty()) {
               flash(result->error, true);
               if (result->topLevel.isEmpty())
                 return;
             }
             done(result->topLevel);
             updateDisk();
           });
}

void MainWindow::openEntry(const QString &entry)
{
  extractEntries({entry}, tempSlot(), tr("opening"), [this](const QStringList &paths) {
    if (paths.isEmpty())
      return;
    // xdg-open's choice, same as for a file on disk.
    QDesktopServices::openUrl(QUrl::fromLocalFile(paths.first()));
  });
}

void MainWindow::openExternal()
{
  const QModelIndex c = cursor();
  if (!c.isValid())
    return;
  if (m_inArchive) {
    const QString entry = ArchiveSession::pathOf(c);
    if (!m_session->isDir(entry))
      openEntry(entry);
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_proxy->pathOf(c)));
}

// -------------------------------------------------------------- compress

void MainWindow::compress(const QString &name)
{
  if (refuseInArchive())
    return;
  if (name.trimmed().isEmpty()) {
    flash(tr("compress needs a name, e.g. :compress photos.tar.zst  (%1)")
            .arg(Archive::writableTypes()), true);
    return;
  }
  const QStringList paths = targets();
  if (paths.isEmpty())
    return;

  QString out = name.trimmed();
  if (out.startsWith(QLatin1String("~/")))
    out = QDir::homePath() + out.mid(1);
  out = QDir::cleanPath(QDir(m_currentDir).absoluteFilePath(out));
  if (paths.contains(out)) {
    flash(tr("compress: %1 is one of the things being packed").arg(name), true);
    return;
  }
  if (QFileInfo::exists(out)
      && QMessageBox::question(this, tr("Compress"),
                               tr("%1 exists and will be replaced.").arg(QFileInfo(out).fileName()),
                               QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
           != QMessageBox::Yes)
    return;

  const QString base = m_currentDir;
  auto result = std::make_shared<Archive::Written>();
  startJob(tr("compressing"),
           [out, paths, base, result](const std::atomic_bool *cancel,
                                      const std::function<void(double)> &progress) {
             *result = Archive::create(out, paths, base, cancel, progress);
           },
           [this, out, paths, result] {
             if (result->cancelled) {
               flash(tr("cancelled, nothing written"));
               return;
             }
             if (!result->error.isEmpty()) {
               flash(result->error, true);
               return;
             }
             clearMarks();
             flash(tr("%n packed into %1  %2", nullptr, paths.size())
                     .arg(QFileInfo(out).fileName(), humanSize(QFileInfo(out).size())));
             if (!m_inArchive && QFileInfo(out).absolutePath() == m_currentDir) {
               m_pendingPath = out;
               tryPending();
             }
             updateDisk();
           });
}

// --------------------------------------------------------------- preview

void MainWindow::previewArchiveFile(const QString &file)
{
  const QFileInfo fi(file);
  const QString title = QMimeDatabase().mimeTypeForFile(fi).name() + QStringLiteral("  ")
                      + humanSize(fi.size());
  if (m_session && m_session->file() == file && !m_session->isStale()) {
    m_preview->showText(title, archiveSummary(m_session->listing()));
    return;
  }
  m_preview->showText(title, tr("reading..."));

  // Bounded: a 20G tarball has to be decompressed to be listed, and the
  // cursor is only passing over it. l reads the whole thing.
  auto cancel = std::make_shared<std::atomic_bool>(false);
  m_previewCancel = cancel;
  const quint64 gen = m_previewGen;
  QPointer<MainWindow> self(this);
  QThreadPool::globalInstance()->start([self, cancel, gen, file, title] {
    const Archive::Listing l = Archive::list(file, QString(), 5000, 1500, cancel.get());
    if (l.cancelled)
      return;
    const QString text = archiveSummary(l);
    QMetaObject::invokeMethod(qApp, [self, gen, title, text] {
      if (self && self->m_previewGen == gen)
        self->m_preview->showText(title, text);
    }, Qt::QueuedConnection);
  });
}

void MainWindow::previewEntry(const QString &entry)
{
  const QString name = entry.section(QLatin1Char('/'), -1);
  const QModelIndex idx = m_session->indexOf(entry);
  const bool encrypted = idx.data(EntryRole::Encrypted).toBool();
  if (encrypted && m_session->password().isEmpty()) {
    m_preview->showText(name, tr("encrypted\n\nl or r asks for the password"));
    return;
  }
  m_preview->showText(name, tr("reading..."));

  const bool image = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension)
                       .name().startsWith(QLatin1String("image/"));
  const qint64 limit = image ? 32 * 1024 * 1024 : 64 * 1024;
  const QString file = m_session->file();
  const QString password = m_session->password();
  auto cancel = std::make_shared<std::atomic_bool>(false);
  m_previewCancel = cancel;
  const quint64 gen = m_previewGen;
  QPointer<MainWindow> self(this);
  QThreadPool::globalInstance()->start([self, cancel, gen, file, password, entry, name, limit] {
    const Archive::Data d = Archive::read(file, password, entry, limit, cancel.get());
    if (d.cancelled)
      return;
    QMetaObject::invokeMethod(qApp, [self, gen, name, d] {
      if (!self || self->m_previewGen != gen)
        return;
      if (d.needPassword)
        self->m_preview->showText(name, tr("encrypted\n\nl or r asks for the password"));
      else if (!d.error.isEmpty())
        self->m_preview->showText(name, tr("can't read it: %1").arg(d.error));
      else
        self->m_preview->showData(name, d.size, d.bytes);
    }, Qt::QueuedConnection);
  });
}

QString MainWindow::entryDetail(const QModelIndex &idx) const
{
  if (!idx.isValid()) {
    const Archive::Listing &l = m_session->listing();
    return QFileInfo(m_session->file()).fileName() + QStringLiteral("  ") + l.format
         + tr("  %n entries", nullptr, l.entries.size());
  }
  const bool dir = idx.data(EntryRole::IsDir).toBool();
  const QString link = idx.data(EntryRole::Link).toString();
  const qint64 size = idx.data(EntryRole::Size).toLongLong();
  const QDateTime mtime = idx.data(EntryRole::MTime).toDateTime();
  QString line = modeString(idx.data(EntryRole::Mode).toUInt(), dir, !link.isEmpty())
               + QStringLiteral("  ")
               + (dir ? QStringLiteral("-") : size >= 0 ? humanSize(size) : QStringLiteral("?"))
               + QStringLiteral("  ")
               + (mtime.isValid() ? mtime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                  : QStringLiteral("----------  -----"))
               + QStringLiteral("  ") + ArchiveSession::pathOf(idx);
  if (!link.isEmpty())
    line += QStringLiteral(" -> ") + link;
  if (idx.data(EntryRole::Encrypted).toBool())
    line += tr("  [encrypted]");
  return line;
}
