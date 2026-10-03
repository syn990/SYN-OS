// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   MainWindow, media previews: a PDF's first page (pdftoppm, pdfinfo), a
//   frame from a tenth of the way into a video (ffmpeg, ffprobe), and an
//   audio file's tags or its cover art (ffprobe, ffmpeg). The programs
//   run in the background and are killed when the cursor moves on; the
//   pictures are kept in ~/.cache/syn-shell/thumbs under a hash of the
//   path, size and modification time, so coming back to a file is
//   instant. A program that isn't installed just means the plain preview.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "MainWindow.h"
#include "Preview.h"
#include "RowDelegate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kThumbWidth = 1024;

bool have(const char *program)
{
  static QHash<QString, bool> known;
  const QString p = QLatin1String(program);
  if (!known.contains(p))
    known.insert(p, !QStandardPaths::findExecutable(p).isEmpty());
  return known.value(p);
}

QString thumbFor(const QString &path)
{
  const QFileInfo fi(path);
  const QByteArray key = QFile::encodeName(fi.absoluteFilePath()) + '|'
                       + QByteArray::number(fi.size()) + '|'
                       + QByteArray::number(fi.lastModified().toSecsSinceEpoch());
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                    + QStringLiteral("/syn-shell/thumbs");
  QDir().mkpath(dir);
  return dir + QLatin1Char('/')
       + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex())
       + QStringLiteral(".png");
}

QString clock(double seconds)
{
  const qint64 s = qint64(seconds);
  return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
                                               .arg(s % 60, 2, 10, QLatin1Char('0'))
                   : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// Runs a program for the preview now showing; `done` gets its output
// only if the cursor is still there when it ends.
void MainWindow::mediaRun(const QString &program, const QStringList &args,
                          std::function<void(int, const QByteArray &)> done)
{
  auto *proc = new QProcess(this);
  m_mediaProcs << proc;
  const quint64 gen = m_previewGen;
  QPointer<MainWindow> self(this);
  connect(proc, &QProcess::finished, this, [self, proc, gen, done](int code, QProcess::ExitStatus st) {
    const QByteArray out = proc->readAllStandardOutput();
    proc->deleteLater();
    if (!self)
      return;
    self->m_mediaProcs.removeOne(proc);
    if (self->m_previewGen == gen)
      done(st == QProcess::NormalExit ? code : -1, out);
  });
  connect(proc, &QProcess::errorOccurred, this, [self, proc, gen, done](QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart)
      return;
    proc->deleteLater();
    if (!self)
      return;
    self->m_mediaProcs.removeOne(proc);
    if (self->m_previewGen == gen)
      done(-1, QByteArray());
  });
  proc->start(program, args);
  QTimer::singleShot(15000, proc, [proc] { proc->kill(); });
}

void MainWindow::killMedia()
{
  for (QProcess *p : std::as_const(m_mediaProcs)) {
    p->disconnect(this);
    p->kill();
    p->deleteLater();
  }
  m_mediaProcs.clear();
}

bool MainWindow::previewMedia(const QString &path, const QString &mime)
{
  const QFileInfo fi(path);
  if (!fi.isFile() || !fi.isReadable())
    return false;
  const QString head = mime + QStringLiteral("  ") + humanSize(fi.size());
  const QString thumb = thumbFor(path);

  if (mime == QLatin1String("application/pdf") && have("pdftoppm")) {
    m_preview->showText(head, tr("rendering the first page..."));
    // The page count and title for the heading, then the page itself.
    auto title = std::make_shared<QString>(head);
    if (have("pdfinfo"))
      mediaRun(QStringLiteral("pdfinfo"), {path}, [this, title, thumb](int code, const QByteArray &out) {
        if (code != 0)
          return;
        QString pages, name;
        for (const QByteArray &line : out.split('\n')) {
          if (line.startsWith("Pages:"))
            pages = QString::fromUtf8(line.mid(6)).trimmed();
          else if (line.startsWith("Title:"))
            name = QString::fromUtf8(line.mid(6)).trimmed();
        }
        if (!pages.isEmpty())
          *title += tr("  %n page(s)", nullptr, pages.toInt());
        if (!name.isEmpty())
          *title += QStringLiteral("  ") + name;
        if (QFileInfo::exists(thumb))
          m_preview->showImageFile(thumb, *title);
      });
    if (QFileInfo::exists(thumb)) {
      m_preview->showImageFile(thumb, *title);
      return true;
    }
    const QString prefix = thumb.chopped(4);
    mediaRun(QStringLiteral("pdftoppm"),
             {QStringLiteral("-png"), QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"),
              QStringLiteral("1"), QStringLiteral("-scale-to"), QString::number(kThumbWidth),
              QStringLiteral("-singlefile"), path, prefix},
             [this, thumb, title](int code, const QByteArray &) {
               if (code == 0 && QFileInfo::exists(thumb))
                 m_preview->showImageFile(thumb, *title);
               else
                 m_preview->showText(*title, tr("couldn't render it (encrypted, or damaged?)"));
             });
    return true;
  }

  const bool video = mime.startsWith(QLatin1String("video/"));
  const bool audio = mime.startsWith(QLatin1String("audio/"));
  if ((!video && !audio) || !have("ffprobe"))
    return false;

  m_preview->showText(head, video ? tr("reading the video...") : tr("reading the tags..."));
  mediaRun(QStringLiteral("ffprobe"),
           {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-print_format"),
            QStringLiteral("json"), QStringLiteral("-show_format"), QStringLiteral("-show_streams"), path},
           [this, path, head, thumb, video](int code, const QByteArray &out) {
             if (code != 0) {
               m_preview->showPath(path);
               return;
             }
             const QJsonObject root = QJsonDocument::fromJson(out).object();
             const QJsonObject format = root.value(QStringLiteral("format")).toObject();
             const double duration = format.value(QStringLiteral("duration")).toString().toDouble();
             QString title = head;
             if (duration > 0)
               title += QStringLiteral("  ") + clock(duration);
             bool cover = false;
             QString codec, details;
             for (const QJsonValue &v : root.value(QStringLiteral("streams")).toArray()) {
               const QJsonObject s = v.toObject();
               const QString type = s.value(QStringLiteral("codec_type")).toString();
               if (type == QLatin1String("video")) {
                 if (s.value(QStringLiteral("disposition")).toObject().value(QStringLiteral("attached_pic")).toInt())
                   cover = true;
                 else if (codec.isEmpty() && video) {
                   codec = s.value(QStringLiteral("codec_name")).toString();
                   title += QStringLiteral("  %1x%2").arg(s.value(QStringLiteral("width")).toInt())
                                                     .arg(s.value(QStringLiteral("height")).toInt());
                 }
               } else if (type == QLatin1String("audio") && codec.isEmpty() && !video) {
                 codec = s.value(QStringLiteral("codec_name")).toString();
                 const int rate = s.value(QStringLiteral("sample_rate")).toString().toInt();
                 details = QStringLiteral("%1  %2 kHz  %3 channel(s)").arg(codec)
                             .arg(rate / 1000.0, 0, 'f', 1).arg(s.value(QStringLiteral("channels")).toInt());
               }
             }
             if (!codec.isEmpty())
               title += QStringLiteral("  ") + codec;

             if (!video) {
               // The tags as text; the cover over them if it has one.
               QStringList lines;
               const QJsonObject tags = format.value(QStringLiteral("tags")).toObject();
               for (const char *k : {"title", "artist", "album", "album_artist", "date", "genre", "track"}) {
                 for (auto it = tags.constBegin(); it != tags.constEnd(); ++it)
                   if (it.key().compare(QLatin1String(k), Qt::CaseInsensitive) == 0) {
                     lines << QStringLiteral("%1  %2").arg(QString::fromLatin1(k).toUpper().leftJustified(12, QLatin1Char(' ')),
                                                           it.value().toString());
                     break;
                   }
               }
               if (!details.isEmpty())
                 lines << QString() << details;
               const int bitrate = format.value(QStringLiteral("bit_rate")).toString().toInt();
               if (bitrate > 0)
                 lines << tr("%1 kbit/s").arg(bitrate / 1000);
               const QString text = lines.join(QLatin1Char('\n'));
               if (!cover || !have("ffmpeg")) {
                 m_preview->showText(title, text);
                 return;
               }
               if (QFileInfo::exists(thumb)) {
                 m_preview->showImageFile(thumb, title);
                 return;
               }
               m_preview->showText(title, text);
               mediaRun(QStringLiteral("ffmpeg"),
                        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
                         QStringLiteral("-an"), QStringLiteral("-frames:v"), QStringLiteral("1"),
                         QStringLiteral("-vf"), QStringLiteral("scale=%1:-2").arg(kThumbWidth / 2),
                         QStringLiteral("-y"), thumb},
                        [this, thumb, title](int code, const QByteArray &) {
                          if (code == 0 && QFileInfo::exists(thumb))
                            m_preview->showImageFile(thumb, title);
                        });
               return;
             }

             // A frame from a tenth of the way in (past any black intro).
             if (QFileInfo::exists(thumb)) {
               m_preview->showImageFile(thumb, title);
               return;
             }
             if (!have("ffmpeg")) {
               m_preview->showText(title, QString());
               return;
             }
             m_preview->showText(title, tr("taking a frame..."));
             mediaRun(QStringLiteral("ffmpeg"),
                      {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
                       QString::number(qMax(0.0, duration * 0.1), 'f', 2), QStringLiteral("-i"), path,
                       QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
                       QStringLiteral("scale=%1:-2").arg(kThumbWidth), QStringLiteral("-y"), thumb},
                      [this, thumb, title](int code, const QByteArray &) {
                        if (code == 0 && QFileInfo::exists(thumb))
                          m_preview->showImageFile(thumb, title);
                        else
                          m_preview->showText(title, tr("couldn't take a frame from it"));
                      });
           });
  return true;
}
