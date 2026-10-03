// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Archive: everything libarchive does for us, as plain blocking calls
//   meant to run on a worker thread. Reads whatever libarchive reads
//   (zip, 7z, rar, tar in any compression, iso, cpio, ar/deb, rpm, cab,
//   lha, xar, and a lone .gz/.xz/.zst/... file as a one-entry archive),
//   writes zip, 7z, tar.*, cpio and iso.
//
//   Entry paths are normalised the same way everywhere: no leading "./"
//   or "/", no trailing "/". Entries with a ".." component are never
//   listed or extracted, so nothing from an archive lands outside the
//   folder it's extracted into; symlinks on the way are refused too
//   (ARCHIVE_EXTRACT_SECURE_SYMLINKS).
//
//   Every long call takes a cancel flag it polls, and reports progress as
//   the share of the archive file read so far (0..1) from the worker thread.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace Archive {

using Progress = std::function<void(double)>;

struct Entry
{
  QString path;
  qint64 size = 0;
  QDateTime mtime;
  uint mode = 0;
  bool dir = false;
  bool symlink = false;
  QString link;
  bool encrypted = false;
};

struct Status
{
  QString error;
  bool needPassword = false;
  bool cancelled = false;
  bool ok() const { return error.isEmpty() && !needPassword && !cancelled; }
};

struct Listing : Status
{
  QList<Entry> entries;
  QString format;      // "ZIP 2.0 (deflation)", "GNU tar format", ...
  QStringList filters; // compression layers outermost first: "zstd", "gzip"
  bool complete = true; // false when a limit stopped the read early
  bool encrypted = false;
  int unsafe = 0;       // entries skipped for a ".." in their path
};

struct Data : Status
{
  QByteArray bytes;
  qint64 size = 0; // the entry's full size; bytes may be a prefix of it
};

struct Written : Status
{
  QStringList topLevel; // what appeared directly in the destination
  int skipped = 0;
};

// Archives `l` walks into. By exact MIME name: docx, odt, epub and jar
// are zips underneath but open in their own apps.
bool isArchiveMime(const QString &mime);
// A single compressed file (.gz, .xz, .zst, ...): one entry, the file.
bool isCompressedFileMime(const QString &mime);
// "photos.tar.zst" -> "photos": the folder name an archive extracts into.
QString stripArchiveSuffix(const QString &fileName);
// Normalises an archive path as described above; empty if unsafe/empty.
QString cleanEntryPath(const QString &raw);

// maxEntries / msBudget of 0 mean no limit.
Listing list(const QString &file, const QString &password, int maxEntries, int msBudget,
             const std::atomic_bool *cancel, const Progress &progress = {});

// Reads at most `limit` bytes of one entry.
Data read(const QString &file, const QString &password, const QString &entry,
          qint64 limit, const std::atomic_bool *cancel);

// Extracts the entries equal to or under any of `entries` (everything if
// empty) into destDir, with `stripPrefix` (an archive directory) taken
// off the front of each path first. Existing files are replaced.
Written extract(const QString &file, const QString &password, const QStringList &entries,
                const QString &stripPrefix, const QString &destDir,
                const std::atomic_bool *cancel, const Progress &progress = {});

// Writes `paths` (recursing into directories, symlinks stored as links)
// into a new archive whose format comes from its extension, names stored
// relative to baseDir. Writes to a .part file and renames on success.
Written create(const QString &archivePath, const QStringList &paths, const QString &baseDir,
               const std::atomic_bool *cancel, const Progress &progress = {});

// The extensions create() understands, for messages.
QString writableTypes();

} // namespace Archive
