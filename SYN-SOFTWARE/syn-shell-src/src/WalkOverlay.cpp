// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   WalkOverlay, WalkUse: see WalkOverlay.h.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "WalkOverlay.h"

#include "RowDelegate.h" // humanSize
#include "SpaceView.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QKeyEvent>
#include <QMimeDatabase>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QStandardPaths>
#include <QUrl>

#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace {

// kinds.c's, in its order
enum { KindPicture, KindVideo, KindAudio, KindArchive, KindDocument, KindCode, KindProgram, KindDiskImage, KindOther };

const qint64 TextMost = 2 << 20; // what the text view reads of a file

// What an ELF file is ("ELF 64-bit executable, x86-64"); empty for none.
QString elfType(const QString &path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QByteArray h = f.read(64);
  if (h.size() < 52 || !h.startsWith("\x7f" "ELF"))
    return {};
  const bool wide = h[4] == 2, little = h[5] == 1;
  auto num = [little](const QByteArray &b, int at, int len) {
    quint64 v = 0;
    for (int i = 0; i < len; ++i) {
      const quint64 byte = uchar(b[at + (little ? i : len - 1 - i)]);
      v |= byte << (8 * i);
    }
    return v;
  };
  if (wide && h.size() < 64)
    return {};
  const int type = int(num(h, 16, 2)), machine = int(num(h, 18, 2));
  const quint64 phoff = wide ? num(h, 32, 8) : num(h, 28, 4);
  const int phsize = int(num(h, wide ? 54 : 42, 2)), phnum = int(num(h, wide ? 56 : 44, 2));
  // a program interpreter: run as a program, though built like a library (PIE)
  bool interp = false;
  if (phnum > 0 && phnum < 1024 && phsize >= 4 && f.seek(qint64(phoff))) {
    const QByteArray ph = f.read(qint64(phsize) * phnum);
    for (int i = 0; i + 4 <= ph.size() && !interp; i += phsize)
      interp = num(ph, i, 4) == 3; // PT_INTERP
  }
  QString what;
  switch (type) {
  case 1: what = QStringLiteral("relocatable object"); break;
  case 2: what = QStringLiteral("executable"); break;
  case 3: what = interp ? QStringLiteral("executable (position-independent)") : QStringLiteral("shared library"); break;
  case 4: what = QStringLiteral("core dump"); break;
  default: what = QStringLiteral("type %1").arg(type); break;
  }
  QString arch;
  switch (machine) {
  case 0x03: arch = QStringLiteral("x86"); break;
  case 0x3e: arch = QStringLiteral("x86-64"); break;
  case 0x28: arch = QStringLiteral("ARM"); break;
  case 0xb7: arch = QStringLiteral("AArch64"); break;
  case 0xf3: arch = QStringLiteral("RISC-V"); break;
  case 0x08: arch = QStringLiteral("MIPS"); break;
  case 0x14: arch = QStringLiteral("PowerPC"); break;
  case 0x15: arch = QStringLiteral("PowerPC 64"); break;
  default: arch = QStringLiteral("machine %1").arg(machine); break;
  }
  return QStringLiteral("ELF %1-bit %2, %3").arg(wide ? 64 : 32).arg(what, arch);
}

// No NUL in its start: text.
bool looksText(const QString &path)
{
  QFile f(path);
  return f.open(QIODevice::ReadOnly) && !f.read(8192).contains('\0');
}

// "-rwxr-xr-x  0755", as ls shows it
QString modeText(mode_t m)
{
  QString s = S_ISDIR(m) ? QStringLiteral("d") : S_ISBLK(m) ? QStringLiteral("b") : S_ISCHR(m) ? QStringLiteral("c")
            : S_ISLNK(m) ? QStringLiteral("l") : S_ISFIFO(m) ? QStringLiteral("p") : S_ISSOCK(m) ? QStringLiteral("s")
                                                                                     : QStringLiteral("-");
  const char *rwx = "rwxrwxrwx";
  for (int i = 0; i < 9; ++i)
    s += (m & (0400 >> i)) ? QChar::fromLatin1(rwx[i]) : QLatin1Char('-');
  if (m & S_ISUID)
    s[3] = (m & S_IXUSR) ? QLatin1Char('s') : QLatin1Char('S');
  if (m & S_ISGID)
    s[6] = (m & S_IXGRP) ? QLatin1Char('s') : QLatin1Char('S');
  if (m & S_ISVTX)
    s[9] = (m & S_IXOTH) ? QLatin1Char('t') : QLatin1Char('T');
  return s + QStringLiteral("  %1").arg(m & 07777, 4, 8, QLatin1Char('0'));
}

QString ownerText(const struct stat &st)
{
  const struct passwd *pw = getpwuid(st.st_uid);
  const struct group *gr = getgrgid(st.st_gid);
  return QStringLiteral("%1 : %2").arg(pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(st.st_uid),
                                       gr ? QString::fromLocal8Bit(gr->gr_name) : QString::number(st.st_gid));
}

// A small file's first line, trimmed: what /sys says.
QString sysLine(const QString &path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QString::fromLocal8Bit(f.readLine(512)).trimmed();
}

// A uevent's KEY=value
QString ueventValue(const QString &path, const QString &key)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QString want = key + QLatin1Char('=');
  for (const QByteArray &l : f.readAll().split('\n'))
    if (l.startsWith(want.toLatin1()))
      return QString::fromLocal8Bit(l.mid(want.size())).trimmed();
  return {};
}

// The fixed font at the size the window's own text is (its zoom).
QFont monoFont(const QWidget *w)
{
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  f.setStyleHint(QFont::Monospace);
  f.setFixedPitch(true);
  f.setPixelSize(QFontInfo(w->font()).pixelSize());
  if (!QFontInfo(f).fixedPitch())
    f.setFamily(QStringLiteral("monospace")); // no fixed font set: fontconfig's
  return f;
}

} // namespace

// ---------------------------------------------------------------- the overlay

WalkOverlay::WalkOverlay(SpaceView *view)
  : QWidget(view), m_view(view)
{
  setObjectName(QStringLiteral("walkOverlay")); // SpaceView hands it its keys while it shows
  setFocusPolicy(Qt::StrongFocus);
  m_text = new QPlainTextEdit(this);
  m_text->setReadOnly(true);
  m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_text->setFrameShape(QFrame::NoFrame);
  m_text->document()->setDocumentMargin(8);
  m_text->installEventFilter(this);
  m_text->hide();
  view->installEventFilter(this); // its size is ours
  hide();
}

bool WalkOverlay::showPicture(const QString &path)
{
  QImageReader r(path);
  r.setAutoTransform(true);
  // no bigger than the screen needs: a huge photo read small
  const QSize s = r.size(), most = m_view->size() * m_view->devicePixelRatioF();
  if (s.isValid() && (s.width() > most.width() || s.height() > most.height()))
    r.setScaledSize(s.scaled(most, Qt::KeepAspectRatio));
  const QImage img = r.read();
  if (img.isNull())
    return false;
  m_image = img;
  m_image.setDevicePixelRatio(1);
  m_title = QFileInfo(path).fileName();
  m_note = s.isValid() ? QStringLiteral("%1 x %2  |  %3").arg(s.width()).arg(s.height()).arg(humanSize(QFileInfo(path).size()))
                       : humanSize(QFileInfo(path).size());
  open(Picture, path, true);
  return true;
}

void WalkOverlay::showText(const QString &path)
{
  QFile f(path);
  QByteArray data;
  if (f.open(QIODevice::ReadOnly))
    data = f.read(TextMost);
  const qint64 size = QFileInfo(path).size();
  m_title = QFileInfo(path).fileName();
  m_note = size > TextMost ? tr("the first %1 of %2").arg(humanSize(TextMost), humanSize(size)) : humanSize(size);
  data.replace('\0', ' ');
  m_text->setFont(monoFont(this)); // each time: the window's zoom may have changed it
  m_text->setPlainText(QString::fromUtf8(data));
  m_text->moveCursor(QTextCursor::Start);
  open(Text, path, true);
}

void WalkOverlay::showInfo(const QString &path, const QString &title, const QStringList &lines, bool openable)
{
  m_title = title;
  m_lines = lines;
  m_note.clear();
  open(Info, path, openable);
}

void WalkOverlay::open(Mode mode, const QString &path, bool openable)
{
  m_mode = mode;
  m_path = path;
  m_openable = openable;
  space_release(m_view->app()); // the mouse is free while this is up
  setGeometry(m_view->rect());
  m_text->setVisible(mode == Text);
  if (mode == Text)
    m_text->setGeometry(panel().adjusted(14, 44, -14, -36));
  if (mode != Picture)
    m_image = QImage();
  show();
  raise();
  if (mode == Text)
    m_text->setFocus(Qt::OtherFocusReason);
  else
    setFocus(Qt::OtherFocusReason);
  update();
}

void WalkOverlay::close(bool recapture)
{
  if (!isVisible())
    return;
  hide();
  m_image = QImage();
  m_text->clear();
  if (recapture && space_walking(m_view->app()) && !m_view->isMini()) {
    space_capture(m_view->app()); // back into the room, the mouse its again
    m_view->setFocus(Qt::OtherFocusReason);
  }
  m_view->update();
}

// Where the picture, the text or the lines are, with their title above
// and the keys below.
QRect WalkOverlay::panel() const
{
  const QRect all = rect();
  if (m_mode == Picture && !m_image.isNull()) {
    const QSize room = all.adjusted(40, 30, -40, -30).size() - QSize(20, 70);
    QSize s = m_image.size();
    if (s.width() < room.width() / 3 && s.height() < room.height() / 3)
      s *= 3; // small pictures made bigger, not to the whole screen
    s.scale(s.boundedTo(room), Qt::KeepAspectRatio);
    const QSize p = s + QSize(20, 70);
    return QRect(all.center() - QPoint(p.width() / 2, p.height() / 2), p);
  }
  if (m_mode == Info) {
    const QFontMetrics fm(monoFont(this));
    const int label = fm.horizontalAdvance(QString(14, QLatin1Char('x')));
    int w = QFontMetrics(font()).horizontalAdvance(m_title) + 60;
    for (const QString &l : m_lines) {
      const int colon = l.indexOf(QLatin1String(": "));
      w = qMax(w, (colon > 0 && colon < 14 ? label + fm.horizontalAdvance(l.mid(colon + 2)) : fm.horizontalAdvance(l)) + 60);
    }
    w = qBound(360, w, all.width() - 40);
    const int h = qMin(all.height() - 40, 44 + int(m_lines.size()) * fm.height() + 20 + 36);
    return QRect(all.center() - QPoint(w / 2, h / 2), QSize(w, h));
  }
  const int mx = all.width() / 14, my = all.height() / 14;
  return all.adjusted(mx, my, -mx, -my);
}

void WalkOverlay::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  const QPalette pal = QApplication::palette();
  const QColor bg = pal.color(QPalette::Window), fg = pal.color(QPalette::Text), hi = pal.color(QPalette::Highlight);
  p.fillRect(rect(), QColor(0, 0, 0, 170));
  const QRect r = panel();
  p.fillRect(r, bg);
  p.setPen(QPen(hi, 2));
  p.drawRect(r.adjusted(1, 1, -1, -1));

  const QFont mono = monoFont(this);
  QFont head = font();
  head.setBold(true);
  const QFontMetrics hm(head);
  // the title, top left; its size and such after it
  p.setFont(head);
  p.setPen(hi);
  const QString title = hm.elidedText(m_title, Qt::ElideMiddle, r.width() - 28);
  p.drawText(r.left() + 14, r.top() + 12 + hm.ascent(), title);
  if (!m_note.isEmpty()) {
    p.setFont(font());
    p.setPen(pal.color(QPalette::PlaceholderText));
    const int x = r.left() + 14 + hm.horizontalAdvance(title) + 16;
    p.drawText(QRect(x, r.top() + 12, r.right() - 14 - x, hm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(font()).elidedText(m_note, Qt::ElideRight, qMax(0, r.right() - 14 - x)));
  }

  if (m_mode == Picture && !m_image.isNull()) {
    const QRect img = r.adjusted(10, 44, -10, -26);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(img, m_image);
  } else if (m_mode == Info) {
    p.setFont(mono);
    const QFontMetrics fm(mono);
    int y = r.top() + 44;
    for (const QString &l : std::as_const(m_lines)) {
      if (y + fm.height() > r.bottom() - 30)
        break;
      // "label: value": the label dim
      const int colon = l.indexOf(QLatin1String(": "));
      p.setPen(fg);
      if (colon > 0 && colon < 14) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(r.left() + 20, y + fm.ascent(), l.left(colon + 1));
        p.setPen(fg);
        p.drawText(r.left() + 20 + fm.horizontalAdvance(QStringLiteral("x").repeated(14)), y + fm.ascent(),
                   fm.elidedText(l.mid(colon + 2), Qt::ElideRight, r.width() - 40 - fm.horizontalAdvance(QStringLiteral("x").repeated(14))));
      } else {
        p.drawText(r.left() + 20, y + fm.ascent(), fm.elidedText(l, Qt::ElideRight, r.width() - 40));
      }
      y += fm.height();
    }
  }

  // the keys, along the bottom
  QString keys = tr("E / Esc  close");
  if (m_openable)
    keys += tr("    Enter  open in its app");
  if (m_mode == Text)
    keys += tr("    arrows, PgUp / PgDn  scroll");
  p.setFont(font());
  p.setPen(pal.color(QPalette::PlaceholderText));
  p.drawText(QRect(r.left() + 14, r.bottom() - 26, r.width() - 28, 22), Qt::AlignLeft | Qt::AlignVCenter, keys);
}

// E, Esc and Enter are the overlay's; the text view scrolls on the rest.
bool WalkOverlay::key(QKeyEvent *e)
{
  const int k = e->key();
  if (k == Qt::Key_Escape || k == Qt::Key_E) {
    if (!e->isAutoRepeat())
      close();
    return true;
  }
  if ((k == Qt::Key_Return || k == Qt::Key_Enter) && m_openable) {
    const QString path = m_path;
    close(false); // the mouse stays free: the app opens over us
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return true;
  }
  return false;
}

void WalkOverlay::keyPressEvent(QKeyEvent *e)
{
  if (key(e))
    return;
  if (m_mode == Text) {
    QScrollBar *v = m_text->verticalScrollBar(), *h = m_text->horizontalScrollBar();
    switch (e->key()) {
    case Qt::Key_Up: v->triggerAction(QAbstractSlider::SliderSingleStepSub); return;
    case Qt::Key_Down: v->triggerAction(QAbstractSlider::SliderSingleStepAdd); return;
    case Qt::Key_PageUp: v->triggerAction(QAbstractSlider::SliderPageStepSub); return;
    case Qt::Key_PageDown:
    case Qt::Key_Space: v->triggerAction(QAbstractSlider::SliderPageStepAdd); return;
    case Qt::Key_Home: v->triggerAction(QAbstractSlider::SliderToMinimum); return;
    case Qt::Key_End: v->triggerAction(QAbstractSlider::SliderToMaximum); return;
    case Qt::Key_Left: h->triggerAction(QAbstractSlider::SliderSingleStepSub); return;
    case Qt::Key_Right: h->triggerAction(QAbstractSlider::SliderSingleStepAdd); return;
    default: break;
    }
  }
  e->accept(); // nothing reaches the room behind
}

void WalkOverlay::mousePressEvent(QMouseEvent *e)
{
  // a click outside the panel closes it, as Esc
  if (!panel().contains(e->position().toPoint()))
    close();
  e->accept();
}

bool WalkOverlay::eventFilter(QObject *o, QEvent *e)
{
  if (o == m_text && e->type() == QEvent::KeyPress)
    return key(static_cast<QKeyEvent *>(e));
  if (o == m_view && e->type() == QEvent::Resize && isVisible()) {
    setGeometry(m_view->rect());
    if (m_mode == Text)
      m_text->setGeometry(panel().adjusted(14, 44, -14, -36));
  }
  return QWidget::eventFilter(o, e);
}

// ---------------------------------------------------------------- using things

WalkUse::WalkUse(SpaceView *view)
  : QObject(view), m_view(view), m_overlay(new WalkOverlay(view))
{
  m_ffplay = QStandardPaths::findExecutable(QStringLiteral("ffplay")); // ffmpeg's; optional
  space_set_player(view->app(), !m_ffplay.isEmpty());
  // Out of the walk (or the view made small): the music stops, the overlay goes.
  m_watch.setInterval(300);
  connect(&m_watch, &QTimer::timeout, this, [this] {
    const bool walking = space_walking(m_view->app());
    if (m_proc && !walking)
      stop();
    if (m_overlay->isVisible() && (!walking || m_view->isMini()))
      m_overlay->close(false);
  });
  m_watch.start();
}

WalkUse::~WalkUse()
{
  // the view is going too: only the music stopped, nothing told
  if (m_proc) {
    disconnect(m_proc, nullptr, this, nullptr);
    m_proc->kill();
    m_proc->waitForFinished(500);
  }
}

bool WalkUse::overlayOpen() const
{
  return m_overlay->isVisible();
}

void WalkUse::use(const QString &path, int kind, int dev)
{
  if (dev != WALK_DEV_NONE || path.startsWith(QLatin1String("/dev/"))) {
    deviceInfo(path, dev);
    return;
  }
  const QFileInfo fi(path);
  // a program is never run from here, whatever its name says
  if (kind == KindProgram || !elfType(path).isEmpty()) {
    fileInfo(path, true);
    return;
  }
  if (kind == KindAudio) {
    play(path);
    return;
  }
  if (kind == KindPicture && m_overlay->showPicture(path))
    return;
  const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
  const bool text = looksText(path);
  if (kind == KindCode || (kind == KindDocument && mime.inherits(QStringLiteral("text/plain")))
      || (kind == KindOther && text)) {
    m_overlay->showText(path);
    return;
  }
  // an executable (a script with no ending, say) goes nowhere that might run it
  if (fi.isExecutable()) {
    fileInfo(path, true);
    return;
  }
  // nothing knows what it is: what it looks like, and Enter to try anyway
  if (kind == KindOther && mime.name() == QLatin1String("application/octet-stream")) {
    fileInfo(path, false);
    return;
  }
  openElsewhere(path);
}

void WalkUse::openElsewhere(const QString &path)
{
  space_release(m_view->app()); // its app opens over the room: the mouse goes there
  QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ---- music: one track at a time, through ffplay ----

void WalkUse::play(const QString &path)
{
  if (m_ffplay.isEmpty())
    return; // the room says it needs ffmpeg
  const bool same = path == m_playing;
  stop();
  if (same)
    return; // E on what plays stops it
  auto *p = new QProcess(this);
  p->setProgram(m_ffplay);
  p->setArguments({QStringLiteral("-nodisp"), QStringLiteral("-autoexit"), QStringLiteral("-loglevel"),
                   QStringLiteral("quiet"), path});
  p->setStandardInputFile(QProcess::nullDevice());
  p->setStandardOutputFile(QProcess::nullDevice());
  p->setStandardErrorFile(QProcess::nullDevice());
  // gone when we are, however we go: killed, crashed or closed
  p->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
  connect(p, &QProcess::finished, this, [this, p] {
    if (p == m_proc) {
      m_proc = nullptr;
      setPlaying(QString());
    }
    p->deleteLater();
  });
  connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError err) {
    if (err == QProcess::FailedToStart && p == m_proc) {
      m_proc = nullptr;
      setPlaying(QString());
      p->deleteLater();
    }
  });
  m_proc = p;
  setPlaying(path);
  p->start();
}

void WalkUse::stop()
{
  if (!m_proc)
    return;
  QProcess *p = m_proc;
  m_proc = nullptr;
  disconnect(p, nullptr, this, nullptr);
  p->kill();
  p->waitForFinished(500);
  delete p;
  setPlaying(QString());
}

void WalkUse::setPlaying(const QString &path)
{
  m_playing = path;
  space_set_playing(m_view->app(), path.isEmpty() ? nullptr : path.toUtf8().constData());
  m_view->update();
}

// ---- what a thing is ----

void WalkUse::fileInfo(const QString &path, bool program)
{
  struct stat st {};
  const QByteArray p8 = QFile::encodeName(path);
  QStringList lines;
  const QFileInfo fi(path);
  const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
  lines << tr("size: %1  (%2 bytes)").arg(humanSize(fi.size())).arg(fi.size());
  if (::stat(p8.constData(), &st) == 0) {
    lines << tr("mode: %1").arg(modeText(st.st_mode));
    lines << tr("owner: %1").arg(ownerText(st));
  }
  lines << tr("modified: %1").arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat));
  lines << tr("type: %1  (%2)").arg(mime.comment(), mime.name());
  const QString elf = elfType(path);
  if (!elf.isEmpty())
    lines << tr("binary: %1").arg(elf);
  lines << QString();
  if (program)
    lines << tr("Programs are never run from the room.");
  else
    lines << tr("Nothing here knows this kind of file.");
  m_overlay->showInfo(path, fi.fileName(), lines, !program);
}

void WalkUse::deviceInfo(const QString &path, int dev)
{
  const QString name = QFileInfo(path).fileName();
  struct stat st {};
  const QByteArray p8 = QFile::encodeName(path);
  const bool ok = ::stat(p8.constData(), &st) == 0;
  QStringList lines;
  QString what;
  if (dev == WALK_DEV_DISK || (ok && S_ISBLK(st.st_mode))) {
    const QString sys = QStringLiteral("/sys/class/block/") + name;
    const qint64 bytes = sysLine(sys + QStringLiteral("/size")).toLongLong() * 512;
    const QString part = sysLine(sys + QStringLiteral("/partition"));
    what = part.isEmpty() ? tr("a disk") : tr("a partition");
    lines << tr("what: %1").arg(what);
    lines << tr("size: %1  (%2 bytes)").arg(humanSize(bytes)).arg(bytes);
    // a partition's model is its disk's
    QString model = sysLine(sys + QStringLiteral("/device/model"));
    QString vendor = sysLine(sys + QStringLiteral("/device/vendor"));
    if (!part.isEmpty()) {
      const QString disk = QFileInfo(QFileInfo(sys).canonicalFilePath()).dir().dirName();
      lines << tr("partition: %1 of %2").arg(part, disk);
      if (model.isEmpty())
        model = sysLine(QStringLiteral("/sys/class/block/") + disk + QStringLiteral("/device/model"));
    }
    if (!model.isEmpty())
      lines << tr("model: %1").arg(vendor.isEmpty() ? model : vendor + QLatin1Char(' ') + model);
    if (sysLine(sys + QStringLiteral("/removable")) == QLatin1String("1"))
      lines << tr("removable: yes");
    if (sysLine(sys + QStringLiteral("/ro")) == QLatin1String("1"))
      lines << tr("read-only: yes");
    // where it (or a partition on it) is mounted
    QFile mounts(QStringLiteral("/proc/mounts"));
    QStringList on;
    if (mounts.open(QIODevice::ReadOnly)) {
      for (const QByteArray &l : mounts.readAll().split('\n')) {
        const QList<QByteArray> f = l.split(' ');
        if (f.size() < 3)
          continue;
        const QString src = QString::fromLocal8Bit(f[0]);
        if (src == path || (part.isEmpty() && src.startsWith(path) && src.size() > path.size())) {
          QString at = QString::fromLocal8Bit(f[1]).replace(QLatin1String("\\040"), QLatin1String(" "));
          on << QStringLiteral("%1 on %2 (%3)").arg(QFileInfo(src).fileName(), at, QString::fromLocal8Bit(f[2]));
        }
      }
    }
    if (on.isEmpty())
      lines << tr("mounted: no");
    for (int i = 0; i < on.size(); ++i)
      lines << (i == 0 ? tr("mounted: %1").arg(on[i]) : QString(14, QLatin1Char(' ')) + on[i]); // under the first
  } else {
    switch (dev) {
    case WALK_DEV_TTY: what = name == QLatin1String("ptmx") ? tr("the pseudo-terminal master: new terminals come from it")
                                                            : tr("a terminal"); break;
    case WALK_DEV_SOUND: what = tr("a sound device (ALSA)"); break;
    case WALK_DEV_GPU: what = tr("a graphics device"); break;
    case WALK_DEV_INPUT: what = tr("an input device"); break;
    case WALK_DEV_VOID:
      what = name == QLatin1String("null")  ? tr("the void: takes anything written and gives nothing back")
           : name == QLatin1String("zero")  ? tr("endless zeros")
           : name == QLatin1String("full")  ? tr("always full: every write fails")
                                            : tr("random bytes, from the kernel");
      break;
    default: what = ok && S_ISCHR(st.st_mode) ? tr("a character device") : tr("a device"); break;
    }
    lines << tr("what: %1").arg(what);
    if (dev == WALK_DEV_INPUT) {
      const QString n = sysLine(QStringLiteral("/sys/class/input/%1/device/name").arg(name));
      if (!n.isEmpty())
        lines << tr("name: %1").arg(n);
    } else if (dev == WALK_DEV_GPU) {
      const QString drv = ueventValue(QStringLiteral("/sys/class/drm/%1/device/uevent").arg(name), QStringLiteral("DRIVER"));
      if (!drv.isEmpty())
        lines << tr("driver: %1").arg(drv);
    }
  }
  if (ok) {
    lines << tr("number: %1:%2").arg(major(st.st_rdev)).arg(minor(st.st_rdev));
    lines << tr("mode: %1").arg(modeText(st.st_mode));
    lines << tr("owner: %1").arg(ownerText(st));
  }
  m_overlay->showInfo(path, path, lines, false);
}
