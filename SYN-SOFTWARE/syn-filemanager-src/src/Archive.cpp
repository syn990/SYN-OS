#include "Archive.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>

#include <archive.h>
#include <archive_entry.h>

#include <memory>

namespace Archive {

namespace {

const QSet<QString> &archiveMimes()
{
  static const QSet<QString> set{
    "application/zip", "application/x-zip", "application/x-zip-compressed",
    "application/x-tar", "application/x-gtar",
    "application/x-compressed-tar", "application/x-bzip2-compressed-tar",
    "application/x-bzip-compressed-tar", "application/x-xz-compressed-tar",
    "application/x-zstd-compressed-tar", "application/x-lz4-compressed-tar",
    "application/x-lzip-compressed-tar", "application/x-lzma-compressed-tar",
    "application/x-tarz",
    "application/x-7z-compressed",
    "application/vnd.rar", "application/x-rar", "application/x-rar-compressed",
    "application/vnd.efi.iso", "application/x-cd-image", "application/x-iso9660-image",
    "application/x-cpio", "application/x-archive",
    "application/vnd.debian.binary-package", "application/x-deb",
    "application/x-rpm", "application/vnd.ms-cab-compressed",
    "application/x-lha", "application/x-lzh-compressed", "application/x-xar",
    "application/java-archive", "application/x-java-archive",
    "application/vnd.android.package-archive",
  };
  return set;
}

const QSet<QString> &compressedFileMimes()
{
  static const QSet<QString> set{
    "application/gzip", "application/x-gzip", "application/x-bzip2", "application/x-bzip",
    "application/x-xz", "application/zstd", "application/x-lz4", "application/x-lzip",
    "application/x-lzma", "application/x-compress",
  };
  return set;
}

struct ReaderDeleter { void operator()(archive *a) const { archive_read_free(a); } };
struct WriterDeleter { void operator()(archive *a) const { archive_write_free(a); } };
using Reader = std::unique_ptr<archive, ReaderDeleter>;
using Writer = std::unique_ptr<archive, WriterDeleter>;

constexpr size_t kBlock = 64 * 1024;

QString errorOf(archive *a)
{
  const char *e = archive_error_string(a);
  return e ? QString::fromUtf8(e) : QStringLiteral("unknown libarchive error");
}

bool isCompressedFile(const QString &file)
{
  return isCompressedFileMime(QMimeDatabase().mimeTypeForFile(file).name());
}

// The one place a reader is set up: every format and filter libarchive
// has, plus "raw" for a lone compressed file, which otherwise matches
// nothing (raw would also accept any plain file, hence only then).
Reader open(const QString &file, const QString &password, Status &status)
{
  Reader a(archive_read_new());
  archive_read_support_filter_all(a.get());
  archive_read_support_format_all(a.get());
  if (isCompressedFile(file))
    archive_read_support_format_raw(a.get());
  if (!password.isEmpty())
    archive_read_add_passphrase(a.get(), password.toUtf8().constData());
  if (archive_read_open_filename(a.get(), QFile::encodeName(file).constData(), 1 << 16)
      != ARCHIVE_OK) {
    status.error = errorOf(a.get());
    return nullptr;
  }
  return a;
}

QString entryPath(archive *a, archive_entry *e, const QString &file)
{
  // A raw (single compressed file) "archive" calls its entry "data".
  if (archive_format(a) == ARCHIVE_FORMAT_RAW)
    return QFileInfo(file).completeBaseName();
  if (const char *u = archive_entry_pathname_utf8(e))
    return cleanEntryPath(QString::fromUtf8(u));
  if (const char *p = archive_entry_pathname(e))
    return cleanEntryPath(QString::fromLocal8Bit(p));
  return QString();
}

// Encrypted entry whose data we couldn't read: almost always a missing
// or wrong passphrase, which libarchive reports only as a string.
void classifyDataError(archive *a, archive_entry *e, const QString &password, Status &status)
{
  const QString err = errorOf(a);
  if (archive_entry_is_encrypted(e) || err.contains(QLatin1String("assphrase"))) {
    status.needPassword = true;
    status.error = password.isEmpty() ? QStringLiteral("password required")
                                      : QStringLiteral("wrong password");
  } else {
    status.error = err;
  }
}

class Reporter
{
public:
  Reporter(archive *a, const QString &file, const Progress &progress)
    : m_a(a), m_total(QFileInfo(file).size()), m_progress(progress) {}
  void operator()()
  {
    if (!m_progress || m_total <= 0 || (m_clock.isValid() && m_clock.elapsed() < 100))
      return;
    m_clock.start();
    m_progress(qBound(0.0, double(archive_filter_bytes(m_a, -1)) / double(m_total), 1.0));
  }

private:
  archive *m_a;
  qint64 m_total;
  const Progress &m_progress;
  QElapsedTimer m_clock;
};

bool cancelled(const std::atomic_bool *cancel)
{
  return cancel && cancel->load();
}

} // namespace

bool isArchiveMime(const QString &mime)
{
  return archiveMimes().contains(mime);
}

bool isCompressedFileMime(const QString &mime)
{
  return compressedFileMimes().contains(mime);
}

QString stripArchiveSuffix(const QString &fileName)
{
  static const QStringList twoPart{".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4",
                                   ".tar.lz", ".tar.lzma", ".tar.Z"};
  for (const QString &s : twoPart)
    if (fileName.endsWith(s, Qt::CaseInsensitive) && fileName.size() > s.size())
      return fileName.left(fileName.size() - s.size());
  const int dot = fileName.lastIndexOf(QLatin1Char('.'));
  return dot > 0 ? fileName.left(dot) : fileName + QStringLiteral("-extracted");
}

QString cleanEntryPath(const QString &raw)
{
  QStringList parts;
  for (const QString &part : raw.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    if (part == QLatin1String("."))
      continue;
    if (part == QLatin1String(".."))
      return QString();
    parts << part;
  }
  return parts.join(QLatin1Char('/'));
}

Listing list(const QString &file, const QString &password, int maxEntries, int msBudget,
             const std::atomic_bool *cancel, const Progress &progress)
{
  Listing out;
  Reader a = open(file, password, out);
  if (!a)
    return out;

  Reporter report(a.get(), file, progress);
  QElapsedTimer clock;
  clock.start();
  archive_entry *e;
  int r;
  while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
    if (cancelled(cancel)) {
      out.cancelled = true;
      return out;
    }
    const QString path = entryPath(a.get(), e, file);
    if (path.isEmpty()) {
      if (archive_entry_pathname(e) && QString::fromLocal8Bit(archive_entry_pathname(e))
                                          .split(QLatin1Char('/')).contains(QLatin1String("..")))
        ++out.unsafe;
      continue;
    }
    Entry entry;
    entry.path = path;
    entry.mode = archive_entry_mode(e);
    entry.dir = archive_entry_filetype(e) == AE_IFDIR;
    entry.symlink = archive_entry_filetype(e) == AE_IFLNK;
    if (entry.symlink && archive_entry_symlink(e))
      entry.link = QString::fromUtf8(archive_entry_symlink(e));
    entry.size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
    if (archive_entry_mtime_is_set(e))
      entry.mtime = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(e));
    entry.encrypted = archive_entry_is_encrypted(e);
    out.encrypted |= entry.encrypted;
    out.entries << entry;
    report();
    if ((maxEntries > 0 && out.entries.size() >= maxEntries)
        || (msBudget > 0 && clock.elapsed() > msBudget)) {
      out.complete = false;
      break;
    }
  }
  if (r == ARCHIVE_FATAL || r == ARCHIVE_FAILED) {
    // A header-encrypted 7z or rar can't even be listed without the key.
    if (archive_read_has_encrypted_entries(a.get()) > 0 && out.entries.isEmpty()) {
      out.needPassword = true;
      out.error = password.isEmpty() ? QStringLiteral("password required")
                                     : QStringLiteral("wrong password");
    } else {
      out.error = errorOf(a.get());
    }
  }

  out.format = QString::fromUtf8(archive_format_name(a.get()));
  // Filter 0 is the outermost layer; the last one is always "none".
  for (int i = 0; i < archive_filter_count(a.get()); ++i) {
    const QString name = QString::fromUtf8(archive_filter_name(a.get(), i));
    if (name != QLatin1String("none"))
      out.filters << name;
  }
  if (archive_format(a.get()) == ARCHIVE_FORMAT_RAW && out.entries.size() == 1
      && out.entries.first().size < 0)
    out.entries.first().size = -1; // raw streams don't record a size
  return out;
}

Data read(const QString &file, const QString &password, const QString &entry,
          qint64 limit, const std::atomic_bool *cancel)
{
  Data out;
  Reader a = open(file, password, out);
  if (!a)
    return out;

  archive_entry *e;
  int r;
  while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
    if (cancelled(cancel)) {
      out.cancelled = true;
      return out;
    }
    if (entryPath(a.get(), e, file) != entry)
      continue;

    out.size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
    out.bytes.resize(qMin<qint64>(limit, out.size >= 0 ? out.size : limit));
    qint64 got = 0;
    while (got < out.bytes.size()) {
      if (cancelled(cancel)) {
        out.cancelled = true;
        return out;
      }
      const la_ssize_t n = archive_read_data(a.get(), out.bytes.data() + got,
                                             size_t(out.bytes.size() - got));
      if (n < 0) {
        classifyDataError(a.get(), e, password, out);
        out.bytes.clear();
        return out;
      }
      if (n == 0)
        break;
      got += n;
    }
    out.bytes.truncate(got);
    if (out.size < 0)
      out.size = got;
    return out;
  }
  out.error = r == ARCHIVE_EOF ? QStringLiteral("not in archive: %1").arg(entry) : errorOf(a.get());
  return out;
}

Written extract(const QString &file, const QString &password, const QStringList &entries,
                const QString &stripPrefix, const QString &dest,
                const std::atomic_bool *cancel, const Progress &progress)
{
  Written out;
  // SECURE_NODOTDOT checks the whole target path, ours included.
  const QString destDir = QDir::cleanPath(QDir(dest).absolutePath());
  QString lastError;
  Reader a = open(file, password, out);
  if (!a)
    return out;

  Writer disk(archive_write_disk_new());
  archive_write_disk_set_options(disk.get(), ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM
                                               | ARCHIVE_EXTRACT_SECURE_SYMLINKS
                                               | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
  archive_write_disk_set_standard_lookup(disk.get());

  const QString prefix = stripPrefix.isEmpty() ? QString() : stripPrefix + QLatin1Char('/');
  auto wanted = [&](const QString &path) {
    if (entries.isEmpty())
      return true;
    for (const QString &want : entries)
      if (path == want || path.startsWith(want + QLatin1Char('/')))
        return true;
    return false;
  };
  auto relative = [&](const QString &path) {
    return path.startsWith(prefix) ? path.mid(prefix.size()) : QString();
  };

  Reporter report(a.get(), file, progress);
  QSet<QString> seenTop;
  archive_entry *e;
  int r;
  while ((r = archive_read_next_header(a.get(), &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
    if (cancelled(cancel)) {
      out.cancelled = true;
      return out;
    }
    const QString path = entryPath(a.get(), e, file);
    if (path.isEmpty()) {
      ++out.skipped;
      continue;
    }
    if (!wanted(path))
      continue;
    const QString rel = relative(path);
    if (rel.isEmpty()) {
      ++out.skipped;
      continue;
    }

    const QString target = destDir + QLatin1Char('/') + rel;
    archive_entry_set_pathname_utf8(e, target.toUtf8().constData());
    if (const char *hl = archive_entry_hardlink(e)) {
      const QString linkRel = relative(cleanEntryPath(QString::fromUtf8(hl)));
      if (linkRel.isEmpty()) {
        ++out.skipped; // its target isn't part of what's being extracted
        continue;
      }
      archive_entry_set_hardlink_utf8(e, (destDir + QLatin1Char('/') + linkRel).toUtf8().constData());
    }

    if (archive_write_header(disk.get(), e) < ARCHIVE_WARN) {
      lastError = errorOf(disk.get());
      ++out.skipped;
      continue;
    }
    const void *buf;
    size_t size;
    la_int64_t offset;
    int dr;
    while ((dr = archive_read_data_block(a.get(), &buf, &size, &offset)) == ARCHIVE_OK) {
      if (cancelled(cancel)) {
        archive_write_finish_entry(disk.get());
        out.cancelled = true;
        return out;
      }
      if (archive_write_data_block(disk.get(), buf, size, offset) < ARCHIVE_WARN) {
        out.error = errorOf(disk.get());
        return out;
      }
      report();
    }
    if (dr != ARCHIVE_EOF) {
      classifyDataError(a.get(), e, password, out);
      archive_write_finish_entry(disk.get());
      QFile::remove(target); // don't leave a half-written file behind
      return out;
    }
    archive_write_finish_entry(disk.get());

    const QString top = rel.section(QLatin1Char('/'), 0, 0);
    if (!seenTop.contains(top)) {
      seenTop.insert(top);
      out.topLevel << destDir + QLatin1Char('/') + top;
    }
    report();
  }
  if (r == ARCHIVE_FATAL || r == ARCHIVE_FAILED)
    out.error = errorOf(a.get());
  else if (out.topLevel.isEmpty() && !lastError.isEmpty())
    out.error = lastError; // nothing came out: say why
  return out;
}

namespace {

// Extension -> (format, filters). Longest suffixes first.
bool configureWriter(archive *a, const QString &name, QString *error)
{
  const QString n = name.toLower();
  struct Rule { const char *suffix; int format; int filter; };
  static const Rule rules[] = {
    {".tar.gz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_GZIP},
    {".tgz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_GZIP},
    {".tar.bz2", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_BZIP2},
    {".tbz2", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_BZIP2},
    {".tar.xz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_XZ},
    {".txz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_XZ},
    {".tar.zst", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_ZSTD},
    {".tzst", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_ZSTD},
    {".tar.lz4", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_LZ4},
    {".tar.lz", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_LZIP},
    {".tar", ARCHIVE_FORMAT_TAR_PAX_RESTRICTED, ARCHIVE_FILTER_NONE},
    {".zip", ARCHIVE_FORMAT_ZIP, ARCHIVE_FILTER_NONE},
    {".7z", ARCHIVE_FORMAT_7ZIP, ARCHIVE_FILTER_NONE},
    {".cpio", ARCHIVE_FORMAT_CPIO_SVR4_NOCRC, ARCHIVE_FILTER_NONE},
    {".iso", ARCHIVE_FORMAT_ISO9660, ARCHIVE_FILTER_NONE},
  };
  for (const Rule &rule : rules) {
    if (!n.endsWith(QLatin1String(rule.suffix)) || n.size() == int(strlen(rule.suffix)))
      continue;
    if (archive_write_set_format(a, rule.format) != ARCHIVE_OK
        || archive_write_add_filter(a, rule.filter) != ARCHIVE_OK) {
      *error = errorOf(a);
      return false;
    }
    if (rule.format == ARCHIVE_FORMAT_ISO9660)
      archive_write_set_format_option(a, "iso9660", "rockridge", "1");
    return true;
  }
  *error = QStringLiteral("unknown archive type, use one of: %1").arg(writableTypes());
  return false;
}

qint64 totalSize(const QStringList &paths)
{
  qint64 total = 0;
  for (const QString &p : paths) {
    const QFileInfo fi(p);
    if (fi.isSymLink() || !fi.isDir()) {
      total += fi.isSymLink() ? 0 : fi.size();
      continue;
    }
    QDirIterator it(p, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      total += it.fileInfo().size();
    }
  }
  return total;
}

} // namespace

QString writableTypes()
{
  return QStringLiteral(".zip .7z .tar .tar.gz .tar.bz2 .tar.xz .tar.zst .tar.lz4 .tar.lz .cpio .iso");
}

Written create(const QString &archivePath, const QStringList &paths, const QString &baseDir,
               const std::atomic_bool *cancel, const Progress &progress)
{
  Written out;
  Writer a(archive_write_new());
  if (!configureWriter(a.get(), archivePath, &out.error))
    return out;

  const QString part = archivePath + QStringLiteral(".part");
  if (archive_write_open_filename(a.get(), QFile::encodeName(part).constData()) != ARCHIVE_OK) {
    out.error = errorOf(a.get());
    return out;
  }
  auto fail = [&](const QString &err) {
    out.error = err;
    a.reset();
    QFile::remove(part);
    return out;
  };

  Reader disk(archive_read_disk_new());
  archive_read_disk_set_symlink_physical(disk.get());
  archive_read_disk_set_standard_lookup(disk.get());

  const qint64 total = qMax<qint64>(1, totalSize(paths));
  qint64 done = 0;
  QElapsedTimer clock;
  clock.start();
  std::unique_ptr<char[]> buf(new char[kBlock]);
  const QDir base(baseDir);
  const QString selfPath = QFileInfo(part).absoluteFilePath();

  for (const QString &p : paths) {
    if (archive_read_disk_open(disk.get(), QFile::encodeName(p).constData()) != ARCHIVE_OK)
      return fail(errorOf(disk.get()));
    for (;;) {
      archive_entry *e = archive_entry_new();
      const int r = archive_read_next_header2(disk.get(), e);
      if (r == ARCHIVE_EOF) {
        archive_entry_free(e);
        break;
      }
      if (r < ARCHIVE_WARN) {
        archive_entry_free(e);
        ++out.skipped; // unreadable file: carry on with the rest
        continue;
      }
      if (cancelled(cancel)) {
        archive_entry_free(e);
        out.cancelled = true;
        a.reset();
        QFile::remove(part);
        return out;
      }
      archive_read_disk_descend(disk.get());

      const QString source = QFile::decodeName(archive_entry_sourcepath(e));
      if (QFileInfo(source).absoluteFilePath() == selfPath) {
        archive_entry_free(e);
        continue; // never pack the archive being written into itself
      }
      archive_entry_set_pathname_utf8(e, base.relativeFilePath(source).toUtf8().constData());
      if (archive_write_header(a.get(), e) < ARCHIVE_WARN) {
        const QString err = errorOf(a.get());
        archive_entry_free(e);
        return fail(err);
      }
      if (archive_entry_filetype(e) == AE_IFREG && archive_entry_size(e) > 0) {
        la_ssize_t n;
        while ((n = archive_read_data(disk.get(), buf.get(), kBlock)) > 0) {
          if (archive_write_data(a.get(), buf.get(), size_t(n)) < 0) {
            const QString err = errorOf(a.get());
            archive_entry_free(e);
            return fail(err);
          }
          done += n;
          if (progress && clock.elapsed() > 100) {
            clock.restart();
            progress(qMin(1.0, double(done) / double(total)));
          }
          if (cancelled(cancel))
            break;
        }
      }
      archive_entry_free(e);
    }
    archive_read_close(disk.get());
  }

  if (archive_write_close(a.get()) != ARCHIVE_OK)
    return fail(errorOf(a.get()));
  a.reset();
  QFile::remove(archivePath);
  if (!QFile::rename(part, archivePath))
    return fail(QStringLiteral("could not move %1 into place").arg(part));
  out.topLevel << archivePath;
  return out;
}

} // namespace Archive
