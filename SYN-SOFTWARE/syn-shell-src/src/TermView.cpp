#include "TermView.h"
#include "PtySession.h"
#include "Zoom.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QDesktopServices>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QVariantAnimation>

#include <cstring>

namespace {

constexpr int kScrollback = 10000;

VTermScreenCell blankCell()
{
  VTermScreenCell c;
  memset(&c, 0, sizeof(c));
  c.width = 1;
  c.fg.type = VTERM_COLOR_DEFAULT_FG;
  c.bg.type = VTERM_COLOR_DEFAULT_BG;
  return c;
}

bool isBlank(const VTermScreenCell &c)
{
  return c.chars[0] == 0 && VTERM_COLOR_IS_DEFAULT_BG(&c.bg) && !c.attrs.reverse;
}

VTermColor rgb(const QColor &q)
{
  VTermColor c;
  vterm_color_rgb(&c, uint8_t(q.red()), uint8_t(q.green()), uint8_t(q.blue()));
  return c;
}

// foot's [colors-dark] (or older [colors]) section: what syn-theme-apply
// writes for the active theme, so this pane and foot always agree.
QHash<QString, QColor> footColours()
{
  QHash<QString, QColor> out;
  QFile f(QDir::homePath() + QStringLiteral("/.config/foot/foot.ini"));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return out;
  QString section;
  QHash<QString, QHash<QString, QColor>> bySection;
  while (!f.atEnd()) {
    const QString line = QString::fromUtf8(f.readLine()).trimmed();
    if (line.startsWith(QLatin1Char('['))) {
      section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1);
      continue;
    }
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0 || line.startsWith(QLatin1Char('#')))
      continue;
    const QString value = line.mid(eq + 1).trimmed();
    const QColor c(QLatin1Char('#') + value);
    if (c.isValid() && value.size() == 6)
      bySection[section].insert(line.left(eq).trimmed(), c);
  }
  out = bySection.value(QStringLiteral("colors-dark"));
  if (out.isEmpty())
    out = bySection.value(QStringLiteral("colors"));
  return out;
}

struct Style
{
  QColor fg, bg;
  bool bold, italic, strike;
  int underline;
  bool operator==(const Style &o) const
  {
    return fg == o.fg && bg == o.bg && bold == o.bold && italic == o.italic
        && strike == o.strike && underline == o.underline;
  }
};

} // namespace

TermView::TermView(QWidget *parent)
  : QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setCursor(Qt::IBeamCursor);

  m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  m_font.setStyleHint(QFont::Monospace);
  m_font.setFixedPitch(true);
  m_font.setKerning(false);
  m_fontPx = QFontInfo(m_font).pixelSize();
  m_font.setPixelSize(m_fontPx);
  const QFontMetrics fm(m_font);
  m_cw = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
  m_ch = qMax(1, fm.height());
  m_ascent = fm.ascent();

  m_zoomAnim = new QVariantAnimation(this);
  m_zoomAnim->setDuration(Zoom::kAnimationMs);
  m_zoomAnim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_zoomAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    m_paintScale = v.toReal() / m_fontPx;
    update();
  });
  connect(m_zoomAnim, &QVariantAnimation::finished, this, [this] {
    applyFont(qRound(m_zoomAnim->endValue().toReal()));
  });
  m_zoomTag.setSingleShot(true);
  m_zoomTag.setInterval(900);
  connect(&m_zoomTag, &QTimer::timeout, this, qOverload<>(&QWidget::update));

  m_vt = vterm_new(m_rows, m_cols);
  vterm_set_utf8(m_vt, 1);
  m_screen = vterm_obtain_screen(m_vt);
  static const VTermScreenCallbacks callbacks = {
    cbDamage, cbMoveRect, cbMoveCursor, cbSetTermProp, cbBell,
    nullptr, cbPushLine, cbPopLine, cbClearScrollback,
  };
  vterm_screen_set_callbacks(m_screen, &callbacks, this);
  static const VTermStateFallbacks fallbacks = {
    nullptr, nullptr, cbOsc, nullptr, nullptr, nullptr, nullptr,
  };
  vterm_screen_set_unrecognised_fallbacks(m_screen, &fallbacks, this);
  vterm_output_set_callback(m_vt, cbOutput, this);
  vterm_screen_enable_altscreen(m_screen, 1);
  vterm_screen_enable_reflow(m_screen, true);
  vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
  reloadColours();
  vterm_screen_reset(m_screen, 1);

  m_sock = new QLocalSocket(this);
  connect(m_sock, &QLocalSocket::connected, this, [this] {
    m_connectTimer.stop();
    m_running = true;
    send(PtySession::Size, QByteArray::number(m_rows) + ' ' + QByteArray::number(m_cols));
  });
  connect(m_sock, &QLocalSocket::readyRead, this, &TermView::onReadyRead);
  connect(m_sock, &QLocalSocket::disconnected, this, &TermView::onDisconnected);
  m_connectTimer.setInterval(20);
  connect(&m_connectTimer, &QTimer::timeout, this, &TermView::tryConnect);
}

TermView::~TermView()
{
  // Dropping the connection hangs the shell up (unless it went to foot).
  m_sock->disconnect(this);
  m_sock->abort();
  vterm_free(m_vt);
}

void TermView::setBackgroundAlpha(double alpha)
{
  m_bgAlpha = alpha;
  update();
}

void TermView::reloadColours()
{
  const QHash<QString, QColor> foot = footColours();
  static const char *const xterm[16] = {
    "#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
    "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff",
  };
  for (int i = 0; i < 16; ++i) {
    const QString key = (i < 8 ? QStringLiteral("regular%1") : QStringLiteral("bright%1")).arg(i % 8);
    m_palette[i] = foot.value(key, QColor(QLatin1String(xterm[i])));
  }
  m_fg = foot.value(QStringLiteral("foreground"), palette().color(QPalette::Text));
  m_bg = foot.value(QStringLiteral("background"), palette().color(QPalette::Window));

  VTermState *state = vterm_obtain_state(m_vt);
  for (int i = 0; i < 16; ++i) {
    const VTermColor c = rgb(m_palette[i]);
    vterm_state_set_palette_color(state, i, &c);
  }
  const VTermColor fg = rgb(m_fg), bg = rgb(m_bg);
  vterm_screen_set_default_colors(m_screen, &fg, &bg);
  update();
}

// ------------------------------------------------------------- session

void TermView::start(const QString &cwd, const QStringList &command)
{
  if (isRunning())
    return;
  resetTerminal();
  m_cwd = cwd;
  m_sockPath = PtySession::newSocketPath();
  QStringList args{QStringLiteral("--pty-hold"), m_sockPath, cwd,
                   QString::number(m_rows), QString::number(m_cols)};
  if (!command.isEmpty())
    args << QStringLiteral("--") << command;
  QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
  m_connectTries = 0;
  m_connectTimer.start();
}

void TermView::tryConnect()
{
  // The holder needs a moment to bind its socket.
  if (++m_connectTries > 150) {
    m_connectTimer.stop();
    emit finished(false);
    return;
  }
  if (m_sock->state() == QLocalSocket::UnconnectedState)
    m_sock->connectToServer(m_sockPath);
}

void TermView::onReadyRead()
{
  const QByteArray data = m_sock->readAll();
  vterm_input_write(m_vt, data.constData(), size_t(data.size()));
  vterm_screen_flush_damage(m_screen);
  update();
}

void TermView::onDisconnected()
{
  if (!m_running)
    return; // a failed connect attempt, not a session
  m_running = false;
  const bool detached = m_detaching;
  m_detaching = false;
  emit finished(detached);
}

void TermView::send(char type, const QByteArray &payload)
{
  if (m_sock->state() == QLocalSocket::ConnectedState)
    m_sock->write(PtySession::packet(PtySession::PacketType(type), payload));
}

void TermView::requestCd(const QString &dir)
{
  if (m_running)
    send(PtySession::Cd, dir.toUtf8());
}

bool TermView::detachToFoot(QString *error)
{
  if (!m_running) {
    *error = tr("no shell running");
    return false;
  }
  m_detaching = true;
  const QString dir = m_cwd.isEmpty() ? QDir::homePath() : m_cwd;
  if (!QProcess::startDetached(QStringLiteral("foot"),
                               {QStringLiteral("-D"), dir, QCoreApplication::applicationFilePath(),
                                QStringLiteral("--attach"), m_sockPath})) {
    m_detaching = false;
    *error = tr("could not start foot");
    return false;
  }
  return true;
}

void TermView::resetTerminal()
{
  vterm_screen_reset(m_screen, 1);
  m_scrollback.clear();
  m_scrollOffset = 0;
  m_hasSelection = false;
  m_altScreen = false;
  m_mouseMode = VTERM_PROP_MOUSE_NONE;
  m_cursorVisible = true;
  m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
  update();
}

// ----------------------------------------------------- vterm callbacks

int TermView::cbDamage(VTermRect, void *user)
{
  static_cast<TermView *>(user)->update();
  return 1;
}

int TermView::cbMoveRect(VTermRect, VTermRect, void *user)
{
  static_cast<TermView *>(user)->update();
  return 1;
}

int TermView::cbMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
  auto *self = static_cast<TermView *>(user);
  self->m_cursor = pos;
  self->m_cursorVisible = visible;
  self->update();
  return 1;
}

int TermView::cbSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
  auto *self = static_cast<TermView *>(user);
  switch (prop) {
  case VTERM_PROP_CURSORVISIBLE: self->m_cursorVisible = val->boolean; break;
  case VTERM_PROP_CURSORSHAPE: self->m_cursorShape = val->number; break;
  case VTERM_PROP_ALTSCREEN:
    self->m_altScreen = val->boolean;
    self->m_scrollOffset = 0;
    break;
  case VTERM_PROP_MOUSE: self->m_mouseMode = val->number; break;
  default: break;
  }
  self->update();
  return 1;
}

int TermView::cbBell(void *)
{
  return 1;
}

int TermView::cbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
  auto *self = static_cast<TermView *>(user);
  int used = cols;
  while (used > 0 && isBlank(cells[used - 1]))
    --used;
  self->m_scrollback.append(QVector<VTermScreenCell>(cells, cells + used));
  if (self->m_scrollback.size() > kScrollback) {
    self->m_scrollback.removeFirst();
    self->m_selAnchor.ry() -= 1;
    self->m_selEnd.ry() -= 1;
  } else if (self->m_hasSelection) {
    // Line numbers count from the oldest line, which didn't change.
  }
  if (self->m_scrollOffset > 0) // reading back: keep the same lines in view
    self->m_scrollOffset = qMin(self->m_scrollOffset + 1, int(self->m_scrollback.size()));
  return 1;
}

int TermView::cbPopLine(int cols, VTermScreenCell *cells, void *user)
{
  auto *self = static_cast<TermView *>(user);
  if (self->m_scrollback.isEmpty())
    return 0;
  const QVector<VTermScreenCell> line = self->m_scrollback.takeLast();
  for (int i = 0; i < cols; ++i)
    cells[i] = i < line.size() ? line[i] : blankCell();
  self->m_scrollOffset = qMin(self->m_scrollOffset, int(self->m_scrollback.size()));
  return 1;
}

int TermView::cbClearScrollback(void *user)
{
  auto *self = static_cast<TermView *>(user);
  self->m_scrollback.clear();
  self->m_scrollOffset = 0;
  self->m_hasSelection = false;
  return 1;
}

int TermView::cbOsc(int command, VTermStringFragment frag, void *user)
{
  if (command != 7)
    return 0;
  auto *self = static_cast<TermView *>(user);
  if (frag.initial)
    self->m_osc.clear();
  self->m_osc.append(frag.str, qsizetype(frag.len));
  if (frag.final) {
    // file://host/path, the path percent-encoded where it has to be.
    const QString url = QString::fromUtf8(self->m_osc);
    if (url.startsWith(QLatin1String("file://"))) {
      const int slash = url.indexOf(QLatin1Char('/'), 7);
      const QString path = slash < 0 ? QString()
                                     : QUrl::fromPercentEncoding(url.mid(slash).toUtf8());
      if (!path.isEmpty() && path != self->m_cwd) {
        self->m_cwd = path;
        emit self->cwdChanged(path);
      }
    }
  }
  return 1;
}

void TermView::cbOutput(const char *s, size_t len, void *user)
{
  static_cast<TermView *>(user)->send(PtySession::Input, QByteArray(s, qsizetype(len)));
}

// ------------------------------------------------------------- drawing

int TermView::firstVisibleLine() const
{
  return int(m_scrollback.size()) - m_scrollOffset;
}

VTermScreenCell TermView::cellAt(int line, int col) const
{
  const int sb = int(m_scrollback.size());
  if (line < sb) {
    const QVector<VTermScreenCell> &l = m_scrollback.at(line);
    return col < l.size() ? l.at(col) : blankCell();
  }
  VTermScreenCell cell;
  if (vterm_screen_get_cell(m_screen, VTermPos{line - sb, col}, &cell))
    return cell;
  return blankCell();
}

QColor TermView::colour(VTermColor c, bool foreground) const
{
  if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c))
    return foreground ? m_fg : m_bg;
  if (VTERM_COLOR_IS_INDEXED(&c) && c.indexed.idx < 16)
    return m_palette[c.indexed.idx];
  vterm_screen_convert_color_to_rgb(m_screen, &c);
  return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
}

bool TermView::isSelected(int line, int col) const
{
  if (!m_hasSelection)
    return false;
  QPoint a = m_selAnchor, b = m_selEnd;
  if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x()))
    std::swap(a, b);
  if (line < a.y() || line > b.y())
    return false;
  if (line == a.y() && col < a.x())
    return false;
  if (line == b.y() && col > b.x())
    return false;
  return true;
}

void TermView::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  // Source, not over: replace what's there (the window's own background)
  // so the opacity is the setting, not the setting twice.
  QColor bg = m_bg;
  bg.setAlphaF(float(m_bgAlpha));
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.fillRect(rect(), bg);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  if (m_paintScale != 1.0) {
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.scale(m_paintScale, m_paintScale);
  }

  QFont bold = m_font, italic = m_font, boldItalic = m_font;
  bold.setBold(true);
  italic.setItalic(true);
  boldItalic.setBold(true);
  boldItalic.setItalic(true);
  const QColor selBg = palette().color(QPalette::Highlight);

  auto styleOf = [&](const VTermScreenCell &c, int line, int col) {
    Style s{colour(c.fg, true), colour(c.bg, false), bool(c.attrs.bold), bool(c.attrs.italic),
            bool(c.attrs.strike), int(c.attrs.underline)};
    if (c.attrs.reverse)
      std::swap(s.fg, s.bg);
    if (isSelected(line, col)) {
      s.bg = selBg;
      s.fg = Qt::white;
    }
    return s;
  };
  auto textOf = [](const VTermScreenCell &c) {
    if (c.chars[0] == 0 || c.chars[0] == uint32_t(-1))
      return QString(QLatin1Char(' '));
    QString t;
    for (int k = 0; k < VTERM_MAX_CHARS_PER_CELL && c.chars[k]; ++k)
      t += QString::fromUcs4(reinterpret_cast<const char32_t *>(&c.chars[k]), 1);
    return t;
  };

  const int first = firstVisibleLine();
  for (int row = 0; row < m_rows; ++row) {
    const int line = first + row;
    const int y = row * m_ch;
    int col = 0;
    while (col < m_cols) {
      const VTermScreenCell c = cellAt(line, col);
      const int width = qMax(1, int(c.width));
      const Style s = styleOf(c, line, col);
      QString text = textOf(c);
      const int start = col;
      col += width;
      // Runs of same-styled single-width cells draw as one string: the
      // font is fixed-pitch, so it lands on the cell grid.
      if (width == 1) {
        while (col < m_cols) {
          const VTermScreenCell n = cellAt(line, col);
          if (n.width != 1 || !(styleOf(n, line, col) == s))
            break;
          text += textOf(n);
          ++col;
        }
      }
      const QRect r(start * m_cw, y, (col - start) * m_cw, m_ch);
      if (s.bg != m_bg)
        p.fillRect(r, s.bg);
      if (!text.trimmed().isEmpty()) {
        p.setFont(s.bold ? (s.italic ? boldItalic : bold) : (s.italic ? italic : m_font));
        p.setPen(s.fg);
        p.drawText(r.left(), y + m_ascent, text);
      }
      if (s.underline) {
        p.setPen(s.fg);
        p.drawLine(r.left(), y + m_ascent + 1, r.right(), y + m_ascent + 1);
      }
      if (s.strike) {
        p.setPen(s.fg);
        p.drawLine(r.left(), y + m_ch / 2, r.right(), y + m_ch / 2);
      }
    }
  }

  // The cursor, only when looking at the live screen.
  if (m_scrollOffset == 0 && m_cursorVisible && m_running) {
    const QRect r(m_cursor.col * m_cw, m_cursor.row * m_ch, m_cw, m_ch);
    const QColor accent = palette().color(QPalette::Highlight);
    if (!hasFocus()) {
      p.setPen(accent);
      p.drawRect(r.adjusted(0, 0, -1, -1));
    } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
      p.fillRect(r.left(), r.bottom() - 1, r.width(), 2, accent);
    } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
      p.fillRect(r.left(), r.top(), 2, r.height(), accent);
    } else {
      p.fillRect(r, accent);
      const VTermScreenCell c = cellAt(firstVisibleLine() + m_cursor.row, m_cursor.col);
      p.setFont(c.attrs.bold ? bold : m_font);
      p.setPen(Qt::white);
      p.drawText(r.left(), r.top() + m_ascent, textOf(c));
    }
  }

  p.resetTransform();
  if (m_scrollOffset > 0 || m_zoomTag.isActive()) {
    // How far back (or the size just zoomed to), top right, waybar style.
    const QString tag = m_zoomTag.isActive()
                          ? QStringLiteral(" %1px ").arg(m_zoomAnim->state() == QAbstractAnimation::Running
                                                           ? qRound(m_zoomAnim->endValue().toReal())
                                                           : m_fontPx)
                          : QStringLiteral(" -%1 ").arg(m_scrollOffset);
    const QFontMetrics fm(m_font);
    const QRect r(width() - fm.horizontalAdvance(tag) - 4, 2, fm.horizontalAdvance(tag), m_ch);
    p.fillRect(r, palette().color(QPalette::Highlight));
    p.setFont(m_font);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, tag);
  }
}

void TermView::resizeEvent(QResizeEvent *)
{
  resizeGrid();
}

void TermView::setFontPixels(int px, bool animate)
{
  px = Zoom::step(px, 0);
  const qreal from = m_fontPx * m_paintScale;
  m_zoomAnim->stop();
  m_zoomTag.start();
  if (!animate || !isVisible()) {
    applyFont(px);
    return;
  }
  if (qFuzzyCompare(from, qreal(px))) {
    applyFont(px);
    return;
  }
  m_zoomAnim->setStartValue(from);
  m_zoomAnim->setEndValue(qreal(px));
  m_zoomAnim->start();
}

void TermView::zoomBy(int steps)
{
  // Steps from where an animation is heading, so quick notches add up.
  const int base = m_zoomAnim->state() == QAbstractAnimation::Running
                     ? qRound(m_zoomAnim->endValue().toReal()) : m_fontPx;
  const int home = QFontInfo(QFontDatabase::systemFont(QFontDatabase::FixedFont)).pixelSize();
  setFontPixels(steps == 0 ? home : Zoom::step(base, steps), true);
}

void TermView::applyFont(int px)
{
  m_paintScale = 1.0;
  const bool changed = px != m_fontPx;
  m_fontPx = px;
  m_font.setPixelSize(px);
  const QFontMetrics fm(m_font);
  m_cw = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
  m_ch = qMax(1, fm.height());
  m_ascent = fm.ascent();
  resizeGrid();
  update();
  if (changed)
    emit zoomChanged(px);
}

void TermView::resizeGrid()
{
  const int cols = qMax(2, width() / m_cw);
  const int rows = qMax(2, height() / m_ch);
  if (cols == m_cols && rows == m_rows)
    return;
  m_cols = cols;
  m_rows = rows;
  vterm_set_size(m_vt, rows, cols);
  vterm_screen_flush_damage(m_screen);
  send(PtySession::Size, QByteArray::number(rows) + ' ' + QByteArray::number(cols));
  update();
}

// --------------------------------------------------------------- input

VTermModifier TermView::modifiers(Qt::KeyboardModifiers m) const
{
  int mod = VTERM_MOD_NONE;
  if (m & Qt::ShiftModifier)
    mod |= VTERM_MOD_SHIFT;
  if (m & Qt::AltModifier)
    mod |= VTERM_MOD_ALT;
  if (m & Qt::ControlModifier)
    mod |= VTERM_MOD_CTRL;
  return VTermModifier(mod);
}

bool TermView::event(QEvent *e)
{
  // Every key belongs to the shell: Tab rather than focus moving on, and
  // nothing window-wide gets to claim a key first.
  if (e->type() == QEvent::ShortcutOverride) {
    e->accept();
    return true;
  }
  if (e->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(e);
    if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
      keyPressEvent(ke);
      return true;
    }
  }
  return QWidget::event(e);
}

void TermView::keyPressEvent(QKeyEvent *e)
{
  const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
  const int key = e->key();

  if ((mods & Qt::ControlModifier) && key == Qt::Key_QuoteLeft) {
    emit toggleFocusRequested();
    return;
  }
  // foot's zoom keys: Ctrl+= or Ctrl++, Ctrl+-, Ctrl+0.
  if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) {
    if (key == Qt::Key_Equal || key == Qt::Key_Plus) {
      zoomBy(1);
      return;
    }
    if (key == Qt::Key_Minus) {
      zoomBy(-1);
      return;
    }
    if (key == Qt::Key_0) {
      zoomBy(0);
      return;
    }
  }
  if ((mods & (Qt::ControlModifier | Qt::ShiftModifier))
      == (Qt::ControlModifier | Qt::ShiftModifier)) {
    switch (key) {
    case Qt::Key_C:
      if (m_hasSelection)
        QApplication::clipboard()->setText(selectedText());
      return;
    case Qt::Key_V: pasteText(QApplication::clipboard()->text()); return;
    case Qt::Key_D: emit detachRequested(); return;
    default: break;
    }
  }
  if (mods == Qt::ShiftModifier) {
    if (key == Qt::Key_PageUp) {
      scrollBy(m_rows / 2);
      return;
    }
    if (key == Qt::Key_PageDown) {
      scrollBy(-m_rows / 2);
      return;
    }
  }
  if (!m_running)
    return;

  const VTermModifier vm = modifiers(mods);
  VTermKey vk = VTERM_KEY_NONE;
  switch (key) {
  case Qt::Key_Return: case Qt::Key_Enter: vk = VTERM_KEY_ENTER; break;
  case Qt::Key_Tab: vk = VTERM_KEY_TAB; break;
  case Qt::Key_Backtab: vk = VTERM_KEY_TAB; break;
  case Qt::Key_Backspace: vk = VTERM_KEY_BACKSPACE; break;
  case Qt::Key_Escape: vk = VTERM_KEY_ESCAPE; break;
  case Qt::Key_Up: vk = VTERM_KEY_UP; break;
  case Qt::Key_Down: vk = VTERM_KEY_DOWN; break;
  case Qt::Key_Left: vk = VTERM_KEY_LEFT; break;
  case Qt::Key_Right: vk = VTERM_KEY_RIGHT; break;
  case Qt::Key_Insert: vk = VTERM_KEY_INS; break;
  case Qt::Key_Delete: vk = VTERM_KEY_DEL; break;
  case Qt::Key_Home: vk = VTERM_KEY_HOME; break;
  case Qt::Key_End: vk = VTERM_KEY_END; break;
  case Qt::Key_PageUp: vk = VTERM_KEY_PAGEUP; break;
  case Qt::Key_PageDown: vk = VTERM_KEY_PAGEDOWN; break;
  default:
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
      vk = VTermKey(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    break;
  }

  m_scrollOffset = 0;
  if (vk != VTERM_KEY_NONE) {
    vterm_keyboard_key(m_vt, vk, key == Qt::Key_Backtab ? VTermModifier(vm | VTERM_MOD_SHIFT) : vm);
  } else if ((mods & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
    vterm_keyboard_unichar(m_vt, uint32_t('a' + (key - Qt::Key_A)), vm);
  } else if ((mods & Qt::ControlModifier) && key > 0 && key < 0x80 && e->text().isEmpty()) {
    vterm_keyboard_unichar(m_vt, uint32_t(key), vm); // Ctrl+[ ] \ / @ and friends
  } else {
    const QList<uint> chars = e->text().toUcs4();
    for (uint u : chars)
      if (u >= 0x20 && u != 0x7f) // control keys were handled as keys above
        vterm_keyboard_unichar(m_vt, u, VTermModifier(vm & ~VTERM_MOD_SHIFT));
  }
  update();
}

void TermView::pasteText(const QString &text)
{
  if (!m_running || text.isEmpty())
    return;
  m_scrollOffset = 0;
  // Bracketed if the shell asked for it: zsh then won't run a pasted line
  // on its own.
  vterm_keyboard_start_paste(m_vt);
  for (uint u : text.toUcs4())
    vterm_keyboard_unichar(m_vt, u == '\n' ? '\r' : u, VTERM_MOD_NONE);
  vterm_keyboard_end_paste(m_vt);
}

void TermView::scrollBy(int lines)
{
  if (m_altScreen)
    return;
  m_scrollOffset = qBound(0, m_scrollOffset + lines, int(m_scrollback.size()));
  update();
}

QPoint TermView::cellAtPixel(const QPoint &p) const
{
  const int col = qBound(0, p.x() / m_cw, m_cols - 1);
  const int row = qBound(0, p.y() / m_ch, m_rows - 1);
  return QPoint(col, firstVisibleLine() + row);
}

bool TermView::forwardMouse() const
{
  // Shift always selects, as in foot.
  return m_mouseMode != VTERM_PROP_MOUSE_NONE
      && !(QApplication::keyboardModifiers() & Qt::ShiftModifier) && m_scrollOffset == 0;
}

void TermView::mousePressEvent(QMouseEvent *e)
{
  setFocus();
  const QPoint cell = cellAtPixel(e->position().toPoint());
  // Ctrl+click: a link opens, a path shows in the browser.
  if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier) && !forwardMouse()
      && openLinkAt(cell))
    return;
  if (forwardMouse() && m_running) {
    const int button = e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3;
    vterm_mouse_move(m_vt, cell.y() - firstVisibleLine(), cell.x(), modifiers(e->modifiers()));
    vterm_mouse_button(m_vt, button, true, modifiers(e->modifiers()));
    return;
  }
  if (e->button() == Qt::LeftButton) {
    m_selecting = true;
    m_hasSelection = false;
    m_selAnchor = m_selEnd = cell;
    update();
  } else if (e->button() == Qt::MiddleButton) {
    pasteText(QApplication::clipboard()->text(QClipboard::Selection));
  }
}

void TermView::mouseMoveEvent(QMouseEvent *e)
{
  const QPoint cell = cellAtPixel(e->position().toPoint());
  if (forwardMouse() && m_running) {
    if (m_mouseMode >= VTERM_PROP_MOUSE_DRAG)
      vterm_mouse_move(m_vt, cell.y() - firstVisibleLine(), cell.x(), modifiers(e->modifiers()));
    return;
  }
  if (m_selecting) {
    m_selEnd = cell;
    m_hasSelection = m_selEnd != m_selAnchor;
    update();
  }
}

void TermView::mouseReleaseEvent(QMouseEvent *e)
{
  if (forwardMouse() && m_running) {
    const int button = e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : 3;
    vterm_mouse_button(m_vt, button, false, modifiers(e->modifiers()));
    return;
  }
  if (m_selecting) {
    m_selecting = false;
    if (m_hasSelection)
      QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
  }
}

void TermView::mouseDoubleClickEvent(QMouseEvent *e)
{
  if (forwardMouse() || e->button() != Qt::LeftButton)
    return;
  // A word: everything around the click up to whitespace.
  const QPoint cell = cellAtPixel(e->position().toPoint());
  auto blank = [&](int col) {
    const VTermScreenCell c = cellAt(cell.y(), col);
    return c.chars[0] == 0 || c.chars[0] == ' ';
  };
  if (blank(cell.x()))
    return;
  int a = cell.x(), b = cell.x();
  while (a > 0 && !blank(a - 1))
    --a;
  while (b < m_cols - 1 && !blank(b + 1))
    ++b;
  m_selAnchor = QPoint(a, cell.y());
  m_selEnd = QPoint(b, cell.y());
  m_hasSelection = true;
  m_selecting = false;
  QApplication::clipboard()->setText(selectedText(), QClipboard::Selection);
  update();
}

void TermView::wheelEvent(QWheelEvent *e)
{
  if (e->modifiers() & Qt::ControlModifier) {
    // Ctrl+wheel zooms; a touchpad's small deltas add up to whole steps.
    m_wheelAcc += e->angleDelta().y();
    while (qAbs(m_wheelAcc) >= 120) {
      zoomBy(m_wheelAcc > 0 ? 1 : -1);
      m_wheelAcc -= m_wheelAcc > 0 ? 120 : -120;
    }
    return;
  }
  const int notches = e->angleDelta().y() / 120;
  if (notches == 0)
    return;
  if (forwardMouse() && m_running) {
    const QPoint cell = cellAtPixel(e->position().toPoint());
    vterm_mouse_move(m_vt, cell.y() - firstVisibleLine(), cell.x(), modifiers(e->modifiers()));
    for (int i = 0; i < qAbs(notches); ++i)
      vterm_mouse_button(m_vt, notches > 0 ? 4 : 5, true, modifiers(e->modifiers()));
  } else if (m_altScreen && m_running) {
    // less, man, a pager: the wheel moves through it, as in foot.
    for (int i = 0; i < qAbs(notches) * 3; ++i)
      vterm_keyboard_key(m_vt, notches > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
  } else {
    scrollBy(notches * 3);
  }
}

// One line of the screen or scrollback as text; colAt[i] is the cell
// column where character i sits (wide characters take two cells).
QString TermView::lineText(int line, QVector<int> *colAt) const
{
  QString text;
  for (int col = 0; col < m_cols; ++col) {
    const VTermScreenCell c = cellAt(line, col);
    if (c.chars[0] == uint32_t(-1))
      continue;
    const QString s = c.chars[0] == 0 ? QStringLiteral(" ")
                                      : QString::fromUcs4(reinterpret_cast<const char32_t *>(&c.chars[0]), 1);
    for (int k = 0; k < s.size(); ++k)
      colAt->append(col);
    text += s;
  }
  return text;
}

bool TermView::openLinkAt(const QPoint &cell)
{
  QVector<int> colAt;
  const QString text = lineText(cell.y(), &colAt);
  int i = int(colAt.indexOf(cell.x()));
  if (i < 0 || text.at(i).isSpace())
    return false;
  // The word under the click, up to spaces, quotes and brackets.
  static const QString stops = QStringLiteral(" \t\"'`<>()[]{}|");
  int a = i, b = i;
  while (a > 0 && !stops.contains(text.at(a - 1)))
    --a;
  while (b + 1 < text.size() && !stops.contains(text.at(b + 1)))
    ++b;
  QString word = text.mid(a, b - a + 1);
  while (!word.isEmpty() && QStringLiteral(".,;!?").contains(word.back()))
    word.chop(1);
  if (word.isEmpty())
    return false;

  static const QRegularExpression url(QStringLiteral("^(https?|ftp|file)://\\S+$"));
  if (url.match(word).hasMatch()) {
    QDesktopServices::openUrl(QUrl(word));
    return true;
  }
  // A path, maybe with a compiler's ":line:column" after it.
  static const QRegularExpression position(QStringLiteral(":\\d+(:\\d+)?:?$"));
  word.remove(position);
  QString path = word;
  if (path.startsWith(QLatin1String("~/")) || path == QLatin1String("~"))
    path = QDir::homePath() + path.mid(1);
  else if (!path.startsWith(QLatin1Char('/')))
    path = m_cwd + QLatin1Char('/') + path;
  if (!QFileInfo::exists(path))
    return false;
  emit pathClicked(QDir::cleanPath(path));
  return true;
}

QString TermView::selectedText() const
{
  if (!m_hasSelection)
    return QString();
  QPoint a = m_selAnchor, b = m_selEnd;
  if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x()))
    std::swap(a, b);
  QStringList lines;
  for (int line = a.y(); line <= b.y(); ++line) {
    const int from = line == a.y() ? a.x() : 0;
    const int to = line == b.y() ? b.x() : m_cols - 1;
    QString text;
    for (int col = from; col <= to; ++col) {
      const VTermScreenCell c = cellAt(line, col);
      if (c.chars[0] == uint32_t(-1))
        continue; // right half of a wide character
      if (c.chars[0] == 0) {
        text += QLatin1Char(' ');
        continue;
      }
      for (int k = 0; k < VTERM_MAX_CHARS_PER_CELL && c.chars[k]; ++k)
        text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&c.chars[k]), 1);
    }
    while (text.endsWith(QLatin1Char(' ')))
      text.chop(1);
    lines << text;
  }
  return lines.join(QLatin1Char('\n'));
}

void TermView::focusInEvent(QFocusEvent *e)
{
  QWidget::focusInEvent(e);
  update();
}

void TermView::focusOutEvent(QFocusEvent *e)
{
  QWidget::focusOutEvent(e);
  update();
}

void TermView::contextMenuEvent(QContextMenuEvent *e)
{
  if (forwardMouse())
    return;
  QMenu menu(this);
  QAction *copy = menu.addAction(tr("Copy\tCtrl+Shift+C"), this, [this] {
    QApplication::clipboard()->setText(selectedText());
  });
  copy->setEnabled(m_hasSelection);
  menu.addAction(tr("Paste\tCtrl+Shift+V"), this, [this] {
    pasteText(QApplication::clipboard()->text());
  });
  menu.addAction(tr("Detach to foot\tCtrl+Shift+D"), this, &TermView::detachRequested)
    ->setEnabled(m_running);
  menu.addAction(tr("Back to the files\tCtrl+`"), this, &TermView::toggleFocusRequested);
  menu.addAction(tr("Hide\t`"), this, &TermView::hideRequested);
  menu.exec(e->globalPos());
}
