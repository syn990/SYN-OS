// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SpaceView: see SpaceView.h. Qt's mouse and keys go to src/space/ as
//   the SDL events its own window would have had, and each frame is drawn
//   into this widget's GL context, looked up through Qt.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "SpaceView.h"

#include "PointerLock.h"

#include "SynIcons.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <cstdlib>
#include <cstring>

namespace {

void *glProc(const char *name)
{
  return reinterpret_cast<void *>(QOpenGLContext::currentContext()->getProcAddress(name));
}

// ---- pictures for the walk's blocks (walk.h, walk_picture) ----

// The file's first lines, if it is text: none for anything with a NUL in it.
QStringList firstLines(const QString &path, int count)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QByteArray head = f.read(6000);
  if (head.contains('\0'))
    return {};
  QStringList lines = QString::fromUtf8(head).split(QLatin1Char('\n'));
  if (lines.size() > count)
    lines = lines.mid(0, count);
  for (QString &l : lines) {
    l.replace(QLatin1Char('\t'), QStringLiteral("    "));
    l.remove(QLatin1Char('\r'));
    if (l.size() > 70)
      l.truncate(70);
  }
  return lines;
}

// A terminal's screen with the code on it, or a printed page for text.
QImage textPicture(const QString &path, bool screen)
{
  const QStringList lines = firstLines(path, screen ? 20 : 30);
  if (lines.isEmpty())
    return {};
  const QPalette pal = QApplication::palette();
  const QSize size = screen ? QSize(480, 300) : QSize(360, 480);
  QImage img(size, QImage::Format_RGBA8888);
  QColor bg = screen ? QColor(6, 10, 9) : QColor(236, 232, 220);
  QColor fg = screen ? pal.color(QPalette::Highlight).lighter(150) : QColor(40, 40, 46);
  img.fill(bg);
  QPainter p(&img);
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPixelSize(screen ? 13 : 11);
  p.setFont(font);
  const int lh = QFontMetrics(font).height();
  int y = 8;
  if (screen) {
    p.setPen(bg.lighter(400));
    p.fillRect(0, 0, size.width(), lh + 6, pal.color(QPalette::Highlight).darker(250));
    p.setPen(fg);
    p.drawText(8, 3 + QFontMetrics(font).ascent(), QFileInfo(path).fileName());
    y = lh + 12;
  }
  p.setPen(fg);
  for (const QString &l : lines) {
    if (y + lh > size.height())
      break;
    p.drawText(10, y + QFontMetrics(font).ascent(), l);
    y += lh;
  }
  return img;
}

// The SYN icon for the file, as syn-shell's own columns draw it.
QImage iconPicture(const QString &path)
{
  const QFileInfo fi(path);
  const QPalette pal = QApplication::palette();
  QImage img(128, 128, QImage::Format_RGBA8888);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  SynIcons::paint(&p, QRectF(12, 12, 104, 104), SynIcons::kindFor(fi.fileName(), false, fi.isExecutable()),
                  pal.color(QPalette::Text), pal.color(QPalette::Highlight), false);
  return img;
}

bool walkPicture(const char *cpath, int kind, int dev, unsigned char **rgba, int *w, int *h)
{
  if (dev != WALK_DEV_NONE)
    return false;
  const QString path = QString::fromUtf8(cpath);
  QImage img;
  if (kind == 0) { // a picture: itself, small
    QImageReader r(path);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (s.isValid())
      r.setScaledSize(s.scaled(256, 256, Qt::KeepAspectRatio));
    img = r.read();
  } else if (kind == 5) { // code: on a terminal's screen
    img = textPicture(path, true);
  } else if (kind == 4) { // a document: a page, if it is text
    img = textPicture(path, false);
  }
  if (img.isNull())
    img = iconPicture(path);
  if (img.isNull())
    return false;
  img = img.convertToFormat(QImage::Format_RGBA8888);
  *w = img.width();
  *h = img.height();
  auto *out = static_cast<unsigned char *>(malloc(size_t(*w) * size_t(*h) * 4));
  if (!out)
    return false;
  for (int y = 0; y < *h; ++y)
    memcpy(out + size_t(y) * size_t(*w) * 4, img.constScanLine(y), size_t(*w) * 4);
  *rgba = out;
  return true;
}

Uint8 sdlButton(Qt::MouseButton b)
{
  switch (b) {
  case Qt::LeftButton: return SDL_BUTTON_LEFT;
  case Qt::RightButton: return SDL_BUTTON_RIGHT;
  case Qt::MiddleButton: return SDL_BUTTON_MIDDLE;
  default: return 0;
  }
}

Uint16 sdlMods(Qt::KeyboardModifiers m)
{
  Uint16 out = 0;
  if (m & Qt::ShiftModifier)
    out |= KMOD_LSHIFT;
  if (m & Qt::ControlModifier)
    out |= KMOD_LCTRL;
  if (m & Qt::AltModifier)
    out |= KMOD_LALT;
  return out;
}

// The keys src/space/ answers to, as SDL names them.
bool sdlKey(const QKeyEvent *e, SDL_Keycode *sym, SDL_Scancode *sc)
{
  const int k = e->key();
  if (k >= Qt::Key_A && k <= Qt::Key_Z) {
    *sym = SDLK_a + (k - Qt::Key_A);
    *sc = SDL_Scancode(SDL_SCANCODE_A + (k - Qt::Key_A));
    return true;
  }
  if (k >= Qt::Key_1 && k <= Qt::Key_9) {
    *sym = SDLK_1 + (k - Qt::Key_1);
    *sc = SDL_Scancode(SDL_SCANCODE_1 + (k - Qt::Key_1));
    return true;
  }
  struct Map { int qt; SDL_Keycode sym; SDL_Scancode sc; };
  static const Map map[] = {
    {Qt::Key_0, SDLK_0, SDL_SCANCODE_0},
    {Qt::Key_Tab, SDLK_TAB, SDL_SCANCODE_TAB},
    {Qt::Key_Backtab, SDLK_TAB, SDL_SCANCODE_TAB},
    {Qt::Key_Space, SDLK_SPACE, SDL_SCANCODE_SPACE},
    {Qt::Key_Backspace, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE},
    {Qt::Key_Return, SDLK_RETURN, SDL_SCANCODE_RETURN},
    {Qt::Key_Enter, SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER},
    {Qt::Key_Escape, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE},
    {Qt::Key_Up, SDLK_UP, SDL_SCANCODE_UP},
    {Qt::Key_Down, SDLK_DOWN, SDL_SCANCODE_DOWN},
    {Qt::Key_Left, SDLK_LEFT, SDL_SCANCODE_LEFT},
    {Qt::Key_Right, SDLK_RIGHT, SDL_SCANCODE_RIGHT},
    {Qt::Key_PageUp, SDLK_PAGEUP, SDL_SCANCODE_PAGEUP},
    {Qt::Key_PageDown, SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN},
    {Qt::Key_F5, SDLK_F5, SDL_SCANCODE_F5},
    {Qt::Key_Plus, SDLK_PLUS, SDL_SCANCODE_EQUALS},
    {Qt::Key_Equal, SDLK_EQUALS, SDL_SCANCODE_EQUALS},
    {Qt::Key_Minus, SDLK_MINUS, SDL_SCANCODE_MINUS},
    {Qt::Key_Shift, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT},
  };
  for (const Map &m : map)
    if (m.qt == k) {
      *sym = m.sym;
      *sc = m.sc;
      return true;
    }
  return false;
}

} // namespace

SpaceView::SpaceView(QWidget *parent)
  : QOpenGLWidget(parent), m_app(space_new())
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  space_on_drop(m_app, &SpaceView::dropped, this);
  walk_picture = walkPicture;
  m_timer.setInterval(16);
  connect(&m_timer, &QTimer::timeout, this, &SpaceView::tick);
  // Walking, a frame as soon as the last one is on the screen: paced by
  // the display, not by a timer that drifts against it.
  connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
    if (space_walking(m_app) && isVisible() && !m_mini && window()->isActiveWindow())
      update(); // not the corner, nor a window in the background: the timer is enough for those
  });
  m_lock = new PointerLock(this);
  connect(m_lock, &PointerLock::moved, this, [this](double dx, double dy) {
    space_look(m_app, float(dx), float(dy));
    update();
  });
  connect(m_lock, &PointerLock::lost, this, [this] { space_release(m_app); });
}

void SpaceView::syncLock()
{
  const bool want = space_captured(m_app) && hasFocus() && isVisible() && !m_mini;
  if (want && !m_lock->isLocked())
    m_lock->lock(); // where it can't be held, the plain mouse still turns you
  else if (!want && m_lock->isLocked())
    m_lock->unlock();
}

SpaceView::~SpaceView()
{
  space_quit(m_app); // the scan's thread, before anything it uses goes
}

void SpaceView::dropped(void *self)
{
  emit static_cast<SpaceView *>(self)->treeDropped();
}

void SpaceView::setActive(bool on)
{
  m_active = on;
  if (on)
    m_timer.start();
  else
    m_timer.stop();
}

void SpaceView::setMini(bool on)
{
  m_mini = on;
  if (on)
    space_release(m_app);
  space_set_mini(m_app, on);
  syncLock();
  setFocusPolicy(on ? Qt::NoFocus : Qt::StrongFocus);
  setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
  setToolTip(on ? tr("Where the space has gone here: click to make it big") : QString());
  m_timer.setInterval(on ? 33 : 16); // the corner at 30 frames a second is plenty
  if (on)
    focusOutEvent(nullptr);
  update();
}

void SpaceView::showDir(const QString &dir)
{
  space_show(m_app, dir.toUtf8().constData());
  tick();
}

void SpaceView::selectPath(const QString &path)
{
  space_select(m_app, path.toUtf8().constData());
  update();
}

// Each 16 ms while in use: a frame if one is wanted, else the scan's news.
void SpaceView::tick()
{
  m_lock->pump();
  if (isVisible()) {
    if (space_busy(m_app))
      update(); // paintGL ticks the scan too
  } else {
    space_tick(m_app);
  }
  poll();
}

void SpaceView::poll()
{
  const unsigned gen = space_generation(m_app);
  const bool live = space_live(m_app);
  if (gen != m_gen || live || m_wasLive) { // and once as a scan ends: its last sizes
    m_gen = gen;
    m_wasLive = live;
    emit treeChanged();
  }
  char focus[4096], sel[4096];
  const int c = space_changes(m_app, focus, sel, sizeof focus);
  if (c & SPACE_FOCUS)
    emit focusRequested(QString::fromUtf8(focus));
  if (c & SPACE_SELECT)
    emit selected(QString::fromUtf8(sel));
  if (c & SPACE_USE) {
    int kind, dev;
    const char *path = space_used(m_app, &kind, &dev);
    emit used(QString::fromUtf8(path), kind, dev);
  }
  syncLock();
}

void SpaceView::initializeGL()
{
  m_gl = space_gl_init(m_app, glProc, float(devicePixelRatioF()));
}

void SpaceView::paintGL()
{
  if (!m_gl)
    return;
  m_lock->pump(); // the mouse's latest, into this frame
  const qreal dpr = devicePixelRatioF();
  space_frame(m_app, int(width() * dpr), int(height() * dpr), float(dpr));
  // What the frame found (a doorway walked through) is handed on after it,
  // not from inside the paint.
  QMetaObject::invokeMethod(this, &SpaceView::poll, Qt::QueuedConnection);
}

void SpaceView::send(const SDL_Event &e)
{
  space_event(m_app, &e);
  poll();
  update();
}

void SpaceView::mousePressEvent(QMouseEvent *e)
{
  if (m_mini) {
    if (e->button() == Qt::LeftButton)
      emit enlargeRequested();
    return;
  }
  setFocus(Qt::MouseFocusReason);
  m_clicks = 1;
  SDL_Event ev{};
  ev.type = SDL_MOUSEBUTTONDOWN;
  ev.button.button = sdlButton(e->button());
  ev.button.state = SDL_PRESSED;
  ev.button.clicks = 1;
  ev.button.x = int(e->position().x());
  ev.button.y = int(e->position().y());
  m_last = e->position();
  if (ev.button.button)
    send(ev);
}

void SpaceView::mouseDoubleClickEvent(QMouseEvent *e)
{
  if (m_mini)
    return;
  m_clicks = 2;
  SDL_Event ev{};
  ev.type = SDL_MOUSEBUTTONDOWN;
  ev.button.button = sdlButton(e->button());
  ev.button.state = SDL_PRESSED;
  ev.button.clicks = 2;
  ev.button.x = int(e->position().x());
  ev.button.y = int(e->position().y());
  if (ev.button.button)
    send(ev);
}

void SpaceView::mouseReleaseEvent(QMouseEvent *e)
{
  if (m_mini)
    return;
  SDL_Event ev{};
  ev.type = SDL_MOUSEBUTTONUP;
  ev.button.button = sdlButton(e->button());
  ev.button.state = SDL_RELEASED;
  ev.button.clicks = Uint8(m_clicks);
  ev.button.x = int(e->position().x());
  ev.button.y = int(e->position().y());
  if (ev.button.button)
    send(ev);
}

void SpaceView::mouseMoveEvent(QMouseEvent *e)
{
  if (m_mini)
    return;
  SDL_Event ev{};
  ev.type = SDL_MOUSEMOTION;
  ev.motion.x = int(e->position().x());
  ev.motion.y = int(e->position().y());
  ev.motion.xrel = int(e->position().x() - m_last.x());
  ev.motion.yrel = int(e->position().y() - m_last.y());
  m_last = e->position();
  send(ev);
}

void SpaceView::wheelEvent(QWheelEvent *e)
{
  if (m_mini)
    return;
  // In notches, as SDL counts them; a touchpad's fractions add up to one.
  m_wheel += e->angleDelta().y() / 120.0;
  const int notches = int(m_wheel);
  if (!notches)
    return;
  m_wheel -= notches;
  SDL_Event ev{};
  ev.type = SDL_MOUSEWHEEL;
  ev.wheel.y = notches;
  send(ev);
}

bool SpaceView::key(QKeyEvent *e, bool down)
{
  SDL_Keycode sym;
  SDL_Scancode sc;
  if (!sdlKey(e, &sym, &sc))
    return false;
  if (down)
    m_held.insert(sc);
  else
    m_held.remove(sc);
  SDL_Event ev{};
  ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  ev.key.keysym.sym = sym;
  ev.key.keysym.scancode = sc;
  ev.key.keysym.mod = sdlMods(e->modifiers());
  send(ev);
  return true;
}

void SpaceView::keyPressEvent(QKeyEvent *e)
{
  // an overlay over the walk (WalkOverlay) has the keys while it shows
  if (QWidget *o = findChild<QWidget *>(QStringLiteral("walkOverlay"), Qt::FindDirectChildrenOnly); o && o->isVisible()) {
    QApplication::sendEvent(o, e);
    return;
  }
  if (e->isAutoRepeat()) {
    e->accept(); // held keys are held, not pressed again
    return;
  }
  if (e->key() == Qt::Key_Escape && !space_walking(m_app) && !space_live(m_app)) {
    emit escaped();
    return;
  }
  if (!key(e, true))
    QOpenGLWidget::keyPressEvent(e);
}

void SpaceView::keyReleaseEvent(QKeyEvent *e)
{
  if (e->isAutoRepeat())
    return;
  if (!key(e, false))
    QOpenGLWidget::keyReleaseEvent(e);
}

void SpaceView::focusOutEvent(QFocusEvent *e)
{
  // Nothing stays held once the keys have gone elsewhere.
  space_release(m_app); // the mouse goes back with the keys
  syncLock();
  for (int sc : std::as_const(m_held)) {
    SDL_Event ev{};
    ev.type = SDL_KEYUP;
    ev.key.keysym.scancode = SDL_Scancode(sc);
    space_event(m_app, &ev);
  }
  m_held.clear();
  if (e)
    QOpenGLWidget::focusOutEvent(e);
}
