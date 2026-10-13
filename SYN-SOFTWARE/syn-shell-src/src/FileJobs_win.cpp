// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   FileJobs, Windows: the trash is the Recycle Bin, the one Explorer and
//   every other program uses. Each drive keeps its own, a folder per user
//   (X:\$Recycle.Bin\<SID>), so nothing crosses drives to be binned.
//   Windows does the binning itself, through QFile::moveToTrash: the file
//   becomes $R + six random characters, and a matching $I file holds
//   where it came from. Restoring is the same by hand, $R back to where
//   $I says and $I gone, which is all Explorer's Restore does too.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holla (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "FileJobs.h"
#include "Win.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <windows.h>
#include <shellapi.h>

namespace FileJobs {

namespace {

// The $I file beside a $R one: "$RAB12CD.txt" -> "$IAB12CD.txt".
QString recordFor(const QString &trashedFile)
{
  const QFileInfo fi(trashedFile);
  const QString name = fi.fileName();
  if (!name.startsWith(QLatin1String("$R"), Qt::CaseInsensitive))
    return QString();
  return fi.absolutePath() + QStringLiteral("/$I") + name.mid(2);
}

} // namespace

QString homeTrash()
{
  return Win::recycleBin(qEnvironmentVariable("SystemDrive", QStringLiteral("C:")));
}

bool isInTrash(const QString &path)
{
  // Something inside a user's own folder of a drive's bin.
  static const QRegularExpression inBin(QStringLiteral("^[A-Za-z]:/\\$Recycle\\.Bin/S-[^/]+/."),
                                        QRegularExpression::CaseInsensitiveOption);
  return inBin.match(QFileInfo(path).absoluteFilePath()).hasMatch();
}

QString originalPath(const QString &trashedFile)
{
  // $I: format version (8 bytes; 1 before Windows 10, 2 since), the size
  // (8), when (8), then the path in UTF-16: 260 characters in version 1,
  // a 4-byte length and that many in version 2, nul-terminated.
  QFile f(recordFor(trashedFile));
  if (!f.open(QIODevice::ReadOnly))
    return QString();
  const QByteArray d = f.readAll();
  if (d.size() < 24)
    return QString();
  qint64 version;
  memcpy(&version, d.constData(), 8);
  int at = 24, chars = 0;
  if (version == 1) {
    chars = 260;
  } else if (version == 2 && d.size() >= 28) {
    qint32 n;
    memcpy(&n, d.constData() + 24, 4);
    chars = n;
    at = 28;
  }
  if (chars <= 0 || d.size() < at + chars * 2)
    return QString();
  QString path = QString::fromUtf16(reinterpret_cast<const char16_t *>(d.constData() + at), chars);
  const int nul = path.indexOf(QChar(0));
  if (nul >= 0)
    path.truncate(nul);
  return QDir::fromNativeSeparators(path);
}

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
    // Qt stops rather than deleting outright when Windows can't bin it
    // (a network share, a drive with no bin, something too big for it).
    QString where;
    if (QFile::moveToTrash(abs, &where) && !where.isEmpty())
      out << Pair{abs, QDir::fromNativeSeparators(where)};
    else
      p.addError(QObject::tr("%1: could not move to the Recycle Bin (Shift+Delete deletes it for good)")
                   .arg(abs));
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
    // The bin is on the drive the file came from: a rename, never a copy.
    if (!MoveFileExW(QDir::toNativeSeparators(t).toStdWString().c_str(),
                     QDir::toNativeSeparators(back).toStdWString().c_str(), 0)) {
      p.addError(QObject::tr("%1: could not restore (%2)").arg(t, Win::errorText(GetLastError())));
    } else {
      QFile::remove(recordFor(t));
      out << Pair{t, back};
    }
    ++p.done;
  }
  return out;
}

void emptyTrash(Jobs::Progress &p)
{
  // Every drive's bin, as Explorer's Empty Recycle Bin does; already
  // confirmed by the caller.
  p.bytes = false;
  p.counts = false;
  const HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr,
                                       SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
  // An empty bin answers with E_UNEXPECTED: nothing to do isn't a failure.
  if (FAILED(hr) && hr != E_UNEXPECTED)
    p.addError(QObject::tr("the Recycle Bin could not be emptied (%1)").arg(Win::errorText(DWORD(hr))));
}

} // namespace FileJobs
