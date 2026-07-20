#include "FileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QMessageBox>
#include <QFileDevice>
#include <cerrno>

namespace {

QString baseName(const QString &path)
{
  return QFileInfo(path).fileName();
}

void showFailures(QWidget *parent, const QString &title, const QStringList &failures)
{
  if (failures.isEmpty())
    return;
  QMessageBox::warning(parent, title,
    QObject::tr("%1 item(s) could not be completed:\n\n%2")
      .arg(failures.size())
      .arg(failures.join('\n')));
}

// True if `destDir` is the same as, or a descendant of, `srcPath` — e.g.
// pasting a directory into itself or into one of its own subfolders,
// which would otherwise make copyDirRecursive/QDir::mkpath recurse into
// a target it's simultaneously reading as source.
bool destinationIsInsideSource(const QString &srcPath, const QString &destDir)
{
  const QString src = QDir(srcPath).absolutePath();
  const QString dest = QDir(destDir).absolutePath();
  if (dest == src)
    return true;
  return dest.startsWith(src + QLatin1Char('/'));
}

// True if copying/moving `srcPath` into `destDir` would land it on top of
// itself — pasting a file back into the same folder it's already in,
// with no rename. Distinct from destinationIsInsideSource (that's about
// directories nesting into themselves); this catches the plain-file case
// where source and computed destination paths are simply identical,
// which would otherwise have copyFile delete the "existing destination"
// (actually the source itself) before there's anything left to copy.
bool destinationIsSameAsSource(const QString &srcPath, const QString &destDir)
{
  const QString src = QDir(srcPath).absolutePath();
  const QString dest = QDir(destDir).absolutePath() + QLatin1Char('/') + baseName(srcPath);
  return dest == src;
}

// Recreates the symlink at `src` pointing at the same target, at `dst`.
// Never dereferences it.
bool copySymlink(const QString &src, const QString &dst, QString *error)
{
  const QString target = QFile::symLinkTarget(src);
  if (target.isEmpty()) {
    if (error) *error = QObject::tr("could not read link target");
    return false;
  }
  if (QFile::exists(dst) || QFileInfo(dst).isSymLink())
    QFile::remove(dst);
  if (!QFile::link(target, dst)) {
    if (error) *error = QObject::tr("could not create link");
    return false;
  }
  return true;
}

// Copies a single file (not a directory) from src to dst, overwriting
// dst if it already exists. Symlinks are recreated, not dereferenced.
bool copyFile(const QString &src, const QString &dst, QString *error)
{
  QFileInfo srcInfo(src);
  if (srcInfo.isSymLink())
    return copySymlink(src, dst, error);

  if (QFile::exists(dst) && !QFile::remove(dst)) {
    if (error) *error = QObject::tr("destination exists and could not be replaced");
    return false;
  }
  if (!QFile::copy(src, dst)) {
    if (error) *error = QObject::tr("copy failed (permission denied?)");
    return false;
  }
  return true;
}

// Recursively copies srcDir's contents into dstDir (dstDir is created if
// needed). Best-effort: collects per-entry failures into `failures`
// rather than aborting the whole tree on the first error. Does not
// follow symlinked subdirectories as if they were real directories —
// each is recreated as a link via copyFile/copySymlink instead, so a
// self-referential symlink can't cause infinite recursion.
void copyDirRecursive(const QString &srcDir, const QString &dstDir, QStringList &failures)
{
  QDir dst;
  if (!dst.mkpath(dstDir)) {
    failures << QObject::tr("%1: could not create destination directory").arg(srcDir);
    return;
  }

  QDirIterator it(srcDir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  while (it.hasNext()) {
    it.next();
    const QFileInfo info = it.fileInfo();
    const QString destPath = dstDir + QLatin1Char('/') + info.fileName();

    if (info.isSymLink()) {
      QString err;
      if (!copySymlink(info.filePath(), destPath, &err))
        failures << QObject::tr("%1: %2").arg(info.filePath(), err);
    } else if (info.isDir()) {
      copyDirRecursive(info.filePath(), destPath, failures);
    } else {
      QString err;
      if (!copyFile(info.filePath(), destPath, &err))
        failures << QObject::tr("%1: %2").arg(info.filePath(), err);
    }
  }
}

bool anyDestinationExists(const QStringList &paths, const QString &destDir)
{
  for (const QString &p : paths) {
    const QString candidate = destDir + QLatin1Char('/') + baseName(p);
    if (QFileInfo::exists(candidate))
      return true;
  }
  return false;
}

} // namespace

namespace FileOps {

void deleteEntries(QWidget *parent, const QStringList &paths)
{
  if (paths.isEmpty())
    return;

  QStringList names;
  for (const QString &p : paths)
    names << baseName(p);

  const auto reply = QMessageBox::question(parent, QObject::tr("Delete"),
    QObject::tr("Permanently delete %1 item(s)?\n\n%2\n\nThis cannot be undone.")
      .arg(paths.size())
      .arg(names.join('\n')),
    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;

  QStringList failures;
  for (const QString &p : paths) {
    QFileInfo info(p);
    bool ok;
    if (info.isSymLink() || info.isFile())
      ok = QFile::remove(p);
    else
      ok = QDir(p).removeRecursively();

    if (!ok)
      failures << QObject::tr("%1: delete failed (permission denied?)").arg(p);
  }

  showFailures(parent, QObject::tr("Delete"), failures);
}

void copyEntries(QWidget *parent, const QStringList &paths, const QString &destDir)
{
  if (paths.isEmpty())
    return;

  for (const QString &p : paths) {
    if (QFileInfo(p).isDir() && destinationIsInsideSource(p, destDir)) {
      QMessageBox::warning(parent, QObject::tr("Copy"),
        QObject::tr("Can't copy \"%1\" into itself or one of its own subfolders.")
          .arg(baseName(p)));
      return;
    }
    if (destinationIsSameAsSource(p, destDir)) {
      QMessageBox::warning(parent, QObject::tr("Copy"),
        QObject::tr("\"%1\" is already in the destination folder.").arg(baseName(p)));
      return;
    }
  }

  if (anyDestinationExists(paths, destDir)) {
    const auto reply = QMessageBox::question(parent, QObject::tr("Copy"),
      QObject::tr("One or more items already exist at the destination and will be "
                  "overwritten. Continue?"),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes)
      return;
  }

  QStringList failures;
  for (const QString &p : paths) {
    QFileInfo info(p);
    const QString dest = destDir + QLatin1Char('/') + info.fileName();

    if (info.isSymLink()) {
      QString err;
      if (!copySymlink(p, dest, &err))
        failures << QObject::tr("%1: %2").arg(p, err);
    } else if (info.isDir()) {
      copyDirRecursive(p, dest, failures);
    } else {
      QString err;
      if (!copyFile(p, dest, &err))
        failures << QObject::tr("%1: %2").arg(p, err);
    }
  }

  showFailures(parent, QObject::tr("Copy"), failures);
}

void moveEntries(QWidget *parent, const QStringList &paths, const QString &destDir)
{
  if (paths.isEmpty())
    return;

  for (const QString &p : paths) {
    if (QFileInfo(p).isDir() && destinationIsInsideSource(p, destDir)) {
      QMessageBox::warning(parent, QObject::tr("Move"),
        QObject::tr("Can't move \"%1\" into itself or one of its own subfolders.")
          .arg(baseName(p)));
      return;
    }
    if (destinationIsSameAsSource(p, destDir)) {
      QMessageBox::warning(parent, QObject::tr("Move"),
        QObject::tr("\"%1\" is already in the destination folder.").arg(baseName(p)));
      return;
    }
  }

  if (anyDestinationExists(paths, destDir)) {
    const auto reply = QMessageBox::question(parent, QObject::tr("Move"),
      QObject::tr("One or more items already exist at the destination and will be "
                  "overwritten. Continue?"),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes)
      return;
  }

  QStringList failures;
  for (const QString &p : paths) {
    const QString dest = destDir + QLatin1Char('/') + baseName(p);

    if (QFileInfo::exists(dest) || QFileInfo(dest).isSymLink())
      QFile::remove(dest);

    if (QFile::rename(p, dest))
      continue; // same-filesystem move succeeded

    // QFile::rename() doesn't expose errno directly; a rename() failure
    // between two real paths on different filesystems is the classic
    // EXDEV case, so fall back to copy-then-delete-source unconditionally
    // rather than trying to distinguish the failure reason further.
    QStringList copyFailures;
    QFileInfo info(p);
    if (info.isSymLink()) {
      QString err;
      if (!copySymlink(p, dest, &err)) {
        failures << QObject::tr("%1: %2").arg(p, err);
        continue;
      }
    } else if (info.isDir()) {
      copyDirRecursive(p, dest, copyFailures);
      if (!copyFailures.isEmpty()) {
        failures << copyFailures;
        continue; // leave source in place since the copy was incomplete
      }
    } else {
      QString err;
      if (!copyFile(p, dest, &err)) {
        failures << QObject::tr("%1: %2").arg(p, err);
        continue;
      }
    }

    // Copy side succeeded — now remove the source.
    QFileInfo srcInfo(p);
    const bool removed = (srcInfo.isSymLink() || srcInfo.isFile())
      ? QFile::remove(p)
      : QDir(p).removeRecursively();
    if (!removed)
      failures << QObject::tr("%1: copied but could not remove original").arg(p);
  }

  showFailures(parent, QObject::tr("Move"), failures);
}

} // namespace FileOps
