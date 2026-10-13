#include "FileJobs.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStorageInfo>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace FileJobs {

namespace {

constexpr qint64 kChunk = 1 << 20;

#ifdef Q_OS_WIN
// What lstat tells Linux, from Windows: a symbolic link or a junction is
// a link, never followed; any other reparse point (OneDrive's
// placeholders, deduplicated files) is the file or folder it looks like.
struct Entry
{
  bool exists = false;
  bool dir = false;
  bool link = false;
  qint64 size = 0;
};

std::wstring native(const QString &path)
{
  return QDir::toNativeSeparators(path).toStdWString();
}

Entry entryOf(const QString &path)
{
  Entry e;
  const std::wstring n = native(path);
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExW(n.c_str(), GetFileExInfoStandard, &a))
    return e;
  e.exists = true;
  e.dir = a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
  e.size = qint64(a.nFileSizeHigh) << 32 | a.nFileSizeLow;
  if (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(n.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
      FindClose(h);
      e.link = fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK || fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
    }
  }
  return e;
}

bool lexists(const QString &path)
{
  return entryOf(path).exists;
}

bool isRealDir(const QString &path)
{
  const Entry e = entryOf(path);
  return e.dir && !e.link;
}

// Windows' reasons as the errno values the moves below test for.
int errnoOf(DWORD error)
{
  switch (error) {
  case ERROR_NOT_SAME_DEVICE: return EXDEV;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS: return EEXIST;
  case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
  case ERROR_ACCESS_DENIED: return EACCES;
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION: return EBUSY;
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND: return ENOENT;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL: return ENOSPC;
  case ERROR_WRITE_PROTECT: return EROFS;
  default: return EIO;
  }
}

// rename(2)'s rules: no copying across drives (that's EXDEV, and a job
// with progress), nothing replaced.
int renameRaw(const QString &from, const QString &to)
{
  return MoveFileExW(native(from).c_str(), native(to).c_str(), 0) ? 0 : errnoOf(GetLastError());
}

// unlink(2) or rmdir(2), whichever the entry needs: a link to a folder is
// removed as a folder on Windows, and a read-only file can't be deleted
// until it isn't.
bool removeEntry(const QString &path, QString *why)
{
  const std::wstring n = native(path);
  const DWORD a = GetFileAttributesW(n.c_str());
  if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY))
    SetFileAttributesW(n.c_str(), a & ~DWORD(FILE_ATTRIBUTE_READONLY));
  const bool ok = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)
                    ? RemoveDirectoryW(n.c_str()) : DeleteFileW(n.c_str());
  if (!ok && why)
    *why = QString::fromLocal8Bit(strerror(errnoOf(GetLastError())));
  return ok;
}
#else
QByteArray enc(const QString &p)
{
  return QFile::encodeName(p);
}

bool lexists(const QString &path)
{
  struct stat st;
  return lstat(enc(path).constData(), &st) == 0;
}

bool isRealDir(const QString &path)
{
  struct stat st;
  return lstat(enc(path).constData(), &st) == 0 && S_ISDIR(st.st_mode);
}

// rename(2), with errno kept for the cross-filesystem test.
int renameRaw(const QString &from, const QString &to)
{
  return ::rename(enc(from).constData(), enc(to).constData()) == 0 ? 0 : errno;
}
#endif

// Bytes and entries under each path, symlinks not followed.
qint64 sizeOf(Jobs::Progress &p, const QString &root, int *itemsOut = nullptr)
{
  qint64 bytes = 0;
  int items = 0;
  std::function<void(const QString &)> walk = [&](const QString &path) {
    if (p.cancel)
      return;
#ifdef Q_OS_WIN
    const Entry st = entryOf(path);
    if (!st.exists)
      return;
    ++items;
    if (!st.dir && !st.link)
      bytes += st.size;
    if (st.dir && !st.link)
      for (const QString &name : QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                      | QDir::Hidden | QDir::System))
        walk(path + QLatin1Char('/') + name);
#else
    struct stat st;
    if (lstat(enc(path).constData(), &st) != 0)
      return;
    ++items;
    if (S_ISREG(st.st_mode))
      bytes += st.st_size;
    if (S_ISDIR(st.st_mode))
      for (const QString &name : QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                      | QDir::Hidden | QDir::System))
        walk(path + QLatin1Char('/') + name);
#endif
  };
  walk(root);
  if (itemsOut)
    *itemsOut = items;
  return bytes;
}

void measure(Jobs::Progress &p, const QStringList &paths)
{
  for (const QString &path : paths) {
    int items = 0;
    p.total += sizeOf(p, path, &items);
    p.itemsTotal += items;
  }
}

bool removeTree(Jobs::Progress &p, const QString &path)
{
  if (p.cancel)
    return false;
  bool ok = true;
#ifdef Q_OS_WIN
  if (isRealDir(path)) {
    for (const QString &name : QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                    | QDir::Hidden | QDir::System))
      ok = removeTree(p, path + QLatin1Char('/') + name) && ok;
    if (p.cancel)
      return false;
  }
  QString why;
  if (!removeEntry(path, &why)) {
    p.addError(QObject::tr("%1: could not remove (%2)").arg(path, why));
    return false;
  }
#else
  if (isRealDir(path)) {
    for (const QString &name : QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                    | QDir::Hidden | QDir::System))
      ok = removeTree(p, path + QLatin1Char('/') + name) && ok;
    if (p.cancel)
      return false;
    if (::rmdir(enc(path).constData()) != 0) {
      p.addError(QObject::tr("%1: could not remove (%2)").arg(path, QString::fromLocal8Bit(strerror(errno))));
      return false;
    }
  } else if (::unlink(enc(path).constData()) != 0) {
    p.addError(QObject::tr("%1: could not remove (%2)").arg(path, QString::fromLocal8Bit(strerror(errno))));
    return false;
  }
#endif
  ++p.items;
  if (!p.bytes)
    p.done = p.items.load();
  return ok;
}

bool copyFileChunked(Jobs::Progress &p, const QString &src, const QString &dst)
{
  QFile in(src);
  if (!in.open(QIODevice::ReadOnly)) {
    p.addError(QObject::tr("%1: %2").arg(src, in.errorString()));
    return false;
  }
  QFile out(dst);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    p.addError(QObject::tr("%1: %2").arg(dst, out.errorString()));
    return false;
  }
  QByteArray buf;
  buf.resize(kChunk);
  for (;;) {
    if (p.cancel) {
      out.close();
      QFile::remove(dst); // never leave a half file under the real name
      return false;
    }
    const qint64 n = in.read(buf.data(), kChunk);
    if (n < 0 || (n > 0 && out.write(buf.constData(), n) != n)) {
      p.addError(QObject::tr("%1: %2").arg(dst, n < 0 ? in.errorString() : out.errorString()));
      out.close();
      QFile::remove(dst);
      return false;
    }
    if (n == 0)
      break;
    p.done += n;
  }
  out.close();
#ifdef Q_OS_WIN
  // The open file's own times and read-only flag: a QFileInfo would give
  // a .lnk shortcut's target's.
  const QDateTime modified = in.fileTime(QFileDevice::FileModificationTime);
  QFile::setPermissions(dst, in.permissions());
  QFile touch(dst);
  if (touch.open(QIODevice::Append))
    touch.setFileTime(modified, QFileDevice::FileModificationTime);
#else
  const QFileInfo info(src);
  QFile::setPermissions(dst, info.permissions());
  QFile touch(dst);
  if (touch.open(QIODevice::Append))
    touch.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
#endif
  return true;
}

// Copies src to dst (dst is the new path itself). Directories merge
// into an existing directory of that name; a file in the way of a file
// is replaced.
bool copyTree(Jobs::Progress &p, const QString &src, const QString &dst)
{
  if (p.cancel)
    return false;
  p.setCurrent(src);
  const QFileInfo info(src);
  bool ok = true;
#ifdef Q_OS_WIN
  // From the entry itself: QFileInfo follows a .lnk shortcut to what it
  // points at, and a shortcut is a plain file to copy.
  const Entry e = entryOf(src);
  if (e.link) {
    if (lexists(dst))
      removeEntry(dst, nullptr);
    // The link's own target, not the resolved one: a relative link stays
    // relative. A junction comes back as a symbolic link to the same
    // folder. Windows lets only Developer Mode or an admin make one.
    const QString target = info.isJunction() ? info.junctionTarget() : info.readSymLink();
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (e.dir)
      flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (target.isEmpty()
        || !CreateSymbolicLinkW(native(dst).c_str(), native(target).c_str(), flags)) {
      p.addError(QObject::tr("%1: could not copy the link").arg(src));
      ok = false;
    }
  } else if (e.dir) {
    if (lexists(dst) && !isRealDir(dst))
      removeEntry(dst, nullptr);
#else
  if (info.isSymLink()) {
    if (lexists(dst))
      ::unlink(enc(dst).constData());
    // readlink, not the resolved target: a relative link stays relative.
    char buf[4096];
    const ssize_t n = ::readlink(enc(src).constData(), buf, sizeof(buf) - 1);
    if (n < 0 || ::symlink(QByteArray(buf, n).constData(), enc(dst).constData()) != 0) {
      p.addError(QObject::tr("%1: could not copy the link").arg(src));
      ok = false;
    }
  } else if (info.isDir()) {
    if (lexists(dst) && !isRealDir(dst))
      ::unlink(enc(dst).constData());
#endif
    if (!QDir().mkpath(dst)) {
      p.addError(QObject::tr("%1: could not create the folder").arg(dst));
      return false;
    }
    for (const QString &name : QDir(src).entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                   | QDir::Hidden | QDir::System)) {
      ok = copyTree(p, src + QLatin1Char('/') + name, dst + QLatin1Char('/') + name) && ok;
      if (p.cancel)
        return false;
    }
    QFile::setPermissions(dst, info.permissions());
#ifdef Q_OS_WIN
  } else if (e.exists) { // no devices, pipes or sockets among files here
#else
  } else if (info.isFile()) {
#endif
    if (isRealDir(dst)) {
      p.addError(QObject::tr("%1: a folder of that name is in the way").arg(dst));
      return false;
    }
    ok = copyFileChunked(p, src, dst);
  } else {
    p.addError(QObject::tr("%1: device, pipe or socket, not copied").arg(src));
    ok = false;
  }
  ++p.items;
  return ok;
}

// Where `src` lands in destDir under `policy`; "" means skip it.
QString destinationFor(Jobs::Progress &p, const QString &src, const QString &destDir,
                       Conflict policy, bool *replace)
{
  *replace = false;
  QString dst = destDir + QLatin1Char('/') + QFileInfo(src).fileName();
  // Pasting into the folder it's already in: a copy beside it.
  if (QFileInfo(src).absolutePath() == QDir(destDir).absolutePath())
    return freeName(dst);
  if (!lexists(dst))
    return dst;
  switch (policy) {
  case Conflict::KeepBoth: return freeName(dst);
  case Conflict::Skip:
    p.setCurrent(QObject::tr("skipped %1").arg(dst));
    return QString();
  case Conflict::Replace: *replace = true; return dst;
  }
  return dst;
}

// The freedesktop trash, from here to the end of the namespace and again
// for trash() onwards; Windows has the Recycle Bin (FileJobs_win.cpp).
#ifndef Q_OS_WIN
QString trashFor(const QString &path)
{
  // The home trash if it's on the same filesystem, else the drive's own.
  const QString home = homeTrash();
  struct stat a, b;
  QDir().mkpath(home);
  if (stat(enc(home).constData(), &a) == 0 && lstat(enc(QFileInfo(path).absolutePath()).constData(), &b) == 0
      && a.st_dev == b.st_dev)
    return home;
  QString top = QStorageInfo(QFileInfo(path).absolutePath()).rootPath();
  if (top.endsWith(QLatin1Char('/')))
    top.chop(1);
  return top + QStringLiteral("/.Trash-%1").arg(getuid());
}
#endif

} // namespace

qint64 folderSize(Jobs::Progress &p, const QString &path, qint64 *items)
{
  int n = 0;
  const qint64 bytes = sizeOf(p, path, &n);
  if (items)
    *items = n > 0 ? n - 1 : 0; // not counting the folder itself
  return bytes;
}

#ifndef Q_OS_WIN
QString homeTrash()
{
  const QByteArray xdg = qgetenv("XDG_DATA_HOME");
  return (xdg.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share")
                        : QString::fromLocal8Bit(xdg))
       + QStringLiteral("/Trash");
}

bool isInTrash(const QString &path)
{
  const QString abs = QFileInfo(path).absoluteFilePath();
  return abs.startsWith(homeTrash() + QStringLiteral("/files/"))
      || abs.contains(QStringLiteral("/.Trash-%1/files/").arg(getuid()));
}

QString originalPath(const QString &trashedFile)
{
  const QFileInfo fi(trashedFile);
  const QString trashDir = QFileInfo(fi.absolutePath()).absolutePath(); // .../Trash
  QFile info(trashDir + QStringLiteral("/info/") + fi.fileName() + QStringLiteral(".trashinfo"));
  if (!info.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  while (!info.atEnd()) {
    const QString line = QString::fromUtf8(info.readLine()).trimmed();
    if (!line.startsWith(QLatin1String("Path=")))
      continue;
    QString path = QUrl::fromPercentEncoding(line.mid(5).toUtf8());
    // Paths in a drive's own trash are relative to the drive's top.
    if (!path.startsWith(QLatin1Char('/')))
      path = QFileInfo(trashDir).absolutePath() + QLatin1Char('/') + path;
    return path;
  }
  return QString();
}
#endif

QString freeName(const QString &path)
{
  if (!lexists(path))
    return path;
  const QFileInfo fi(path);
  const QString dir = fi.absolutePath();
  QString stem = fi.fileName(), ext;
  // "photo.tar.gz" keeps its whole extension; a dotfile has none.
  const int dot = stem.indexOf(QLatin1Char('.'), 1);
  if (dot > 0 && !fi.isDir()) {
    ext = stem.mid(dot);
    stem = stem.left(dot);
  }
  for (int i = 2;; ++i) {
    const QString candidate = dir + QLatin1Char('/') + stem + QStringLiteral(" (%1)").arg(i) + ext;
    if (!lexists(candidate))
      return candidate;
  }
}

Pairs copy(Jobs::Progress &p, const QStringList &sources, const QString &destDir, Conflict policy)
{
  Pairs out;
  measure(p, sources);
  const QStorageInfo disk(destDir);
  if (disk.isValid() && disk.bytesAvailable() < p.total) {
    p.addError(QObject::tr("not enough space in %1: needs %2 MB, %3 MB free")
                 .arg(destDir).arg(p.total / 1048576).arg(disk.bytesAvailable() / 1048576));
    return out;
  }
  for (const QString &src : sources) {
    if (p.cancel)
      break;
    bool replace;
    const QString dst = destinationFor(p, src, destDir, policy, &replace);
    if (dst.isEmpty()) {
      p.done += sizeOf(p, src); // skipped: its share counts as done
      continue;
    }
#ifdef Q_OS_WIN
    if (replace && !isRealDir(dst) && isRealDir(src))
      removeEntry(dst, nullptr);
#else
    if (replace && !isRealDir(dst) && QFileInfo(src).isDir())
      ::unlink(enc(dst).constData());
#endif
    if (copyTree(p, src, dst))
      out << Pair{src, dst};
  }
  return out;
}

Pairs move(Jobs::Progress &p, const QStringList &sources, const QString &destDir, Conflict policy)
{
  Pairs out;
  // Renames first: instant, and they don't need measuring.
  QList<Pair> needCopy;
  p.itemsTotal += int(sources.size());
  for (const QString &src : sources) {
    if (p.cancel)
      break;
    bool replace;
    const QString dst = destinationFor(p, src, destDir, policy, &replace);
    if (dst.isEmpty())
      continue;
    p.setCurrent(src);
    if (replace && lexists(dst) && !(isRealDir(src) && isRealDir(dst)))
      removeTree(p, dst);
    const int err = renameRaw(src, dst);
    if (err == 0) {
      out << Pair{src, dst};
      ++p.items;
    } else if (err == EXDEV || err == ENOTEMPTY || err == EEXIST) {
      needCopy << Pair{src, dst}; // another drive, or merging into a folder
    } else {
      p.addError(QObject::tr("%1: %2").arg(src, QString::fromLocal8Bit(strerror(err))));
    }
  }
  if (needCopy.isEmpty())
    return out;

  QStringList from;
  for (const Pair &pr : needCopy)
    from << pr.from;
  measure(p, from);
  const QStorageInfo disk(destDir);
  if (disk.isValid() && disk.bytesAvailable() < p.total - p.done) {
    p.addError(QObject::tr("not enough space in %1").arg(destDir));
    return out;
  }
  for (const Pair &pr : needCopy) {
    if (p.cancel)
      break;
    const int before = int(p.errors().size());
    if (copyTree(p, pr.from, pr.to) && int(p.errors().size()) == before) {
      removeTree(p, pr.from); // only once the copy is whole
      out << pr;
    }
  }
  return out;
}

Pairs moveBack(Jobs::Progress &p, const Pairs &moved)
{
  Pairs out;
  p.itemsTotal += int(moved.size());
  for (const Pair &pr : moved) {
    if (p.cancel)
      break;
    if (!lexists(pr.to)) {
      p.addError(QObject::tr("%1: no longer there").arg(pr.to));
      continue;
    }
    QDir().mkpath(QFileInfo(pr.from).absolutePath());
    const QString back = freeName(pr.from);
    p.setCurrent(pr.to);
    int err = renameRaw(pr.to, back);
    if (err == EXDEV) {
      measure(p, {pr.to});
      if (copyTree(p, pr.to, back))
        removeTree(p, pr.to);
      err = 0;
    }
    if (err)
      p.addError(QObject::tr("%1: %2").arg(pr.to, QString::fromLocal8Bit(strerror(err))));
    else
      out << Pair{pr.to, back};
    ++p.items;
  }
  return out;
}

Pairs remove(Jobs::Progress &p, const QStringList &paths)
{
  Pairs out;
  p.bytes = false;
  measure(p, paths);
  p.total = p.itemsTotal.load();
  for (const QString &path : paths) {
    if (p.cancel)
      break;
    p.setCurrent(path);
    if (removeTree(p, path))
      out << Pair{path, QString()};
  }
  return out;
}

#ifndef Q_OS_WIN
Pairs trash(Jobs::Progress &p, const QStringList &paths)
{
  Pairs out;
  p.bytes = false;
  p.total = paths.size();
  for (const QString &path : paths) {
    if (p.cancel)
      break;
    const QString abs = QFileInfo(path).absoluteFilePath();
    p.setCurrent(abs);
    QString dir = trashFor(abs);
    // A drive whose top folder can't take a .Trash-UID (not yours, or
    // read-only there): the home trash instead, copied across below.
    if (!QDir().mkpath(dir + QStringLiteral("/files")) || !QDir().mkpath(dir + QStringLiteral("/info")))
      dir = homeTrash();
    const bool home = dir == homeTrash();
    if (!QDir().mkpath(dir + QStringLiteral("/files")) || !QDir().mkpath(dir + QStringLiteral("/info"))) {
      p.addError(QObject::tr("%1: no trash folder could be made in %2").arg(abs, dir));
      continue;
    }
    if (!home)
      ::chmod(enc(dir).constData(), 0700);
    // Reserve the name by creating its .trashinfo exclusively.
    const QString base = QFileInfo(abs).fileName();
    QString name = base;
    QFile info;
    bool reserved = false;
    for (int i = 2; i < 10000 && !reserved; ++i) {
      info.setFileName(dir + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo"));
      reserved = !lexists(dir + QStringLiteral("/files/") + name)
              && info.open(QIODevice::WriteOnly | QIODevice::NewOnly);
      if (!reserved)
        name = base + QLatin1Char('.') + QString::number(i);
    }
    if (!reserved) {
      p.addError(QObject::tr("%1: the trash in %2 can't be written").arg(abs, dir));
      continue;
    }
    const QString stored = home ? abs : QDir(QFileInfo(dir).absolutePath()).relativeFilePath(abs);
    info.write("[Trash Info]\nPath=" + QUrl::toPercentEncoding(stored, "/")
               + "\nDeletionDate=" + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + "\n");
    info.close();
    const QString target = dir + QStringLiteral("/files/") + name;
    int err = renameRaw(abs, target);
    if (err == EXDEV) {
      // Into the home trash from another drive: copy, then remove.
      measure(p, {abs});
      const int before = int(p.errors().size());
      err = copyTree(p, abs, target) && int(p.errors().size()) == before && removeTree(p, abs) ? 0 : EIO;
      if (err && !p.cancel)
        removeTree(p, target);
    }
    if (err) {
      info.remove();
      p.addError(QObject::tr("%1: could not move to the trash (%2)")
                   .arg(abs, QString::fromLocal8Bit(strerror(err))));
    } else {
      out << Pair{abs, target};
    }
    ++p.done;
    ++p.items;
  }
  return out;
}

Pairs restore(Jobs::Progress &p, const QStringList &trashed)
{
  Pairs out;
  p.bytes = false;
  p.total = trashed.size();
  for (const QString &t : trashed) {
    if (p.cancel)
      break;
    p.setCurrent(t);
    const QString original = originalPath(t);
    if (original.isEmpty()) {
      p.addError(QObject::tr("%1: no record of where it came from").arg(t));
      ++p.done;
      continue;
    }
    QDir().mkpath(QFileInfo(original).absolutePath());
    const QString back = freeName(original);
    int err = renameRaw(t, back);
    if (err == EXDEV) {
      measure(p, {t});
      err = copyTree(p, t, back) && removeTree(p, t) ? 0 : EIO;
    }
    if (err) {
      p.addError(QObject::tr("%1: could not restore (%2)").arg(t, QString::fromLocal8Bit(strerror(err))));
    } else {
      const QFileInfo fi(t);
      QFile::remove(QFileInfo(fi.absolutePath()).absolutePath() + QStringLiteral("/info/")
                    + fi.fileName() + QStringLiteral(".trashinfo"));
      out << Pair{t, back};
    }
    ++p.done;
  }
  return out;
}

void emptyTrash(Jobs::Progress &p)
{
  const QString dir = homeTrash();
  QStringList all;
  for (const QString &sub : {QStringLiteral("files"), QStringLiteral("info")})
    for (const QString &name : QDir(dir + QLatin1Char('/') + sub)
                                 .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
      all << dir + QLatin1Char('/') + sub + QLatin1Char('/') + name;
  remove(p, all);
}
#endif

} // namespace FileJobs
