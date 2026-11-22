// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   PointerLock: see PointerLock.h.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "PointerLock.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

#ifdef SYN_HAVE_POINTER_LOCK
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#endif

#include <cstring>

#ifdef SYN_HAVE_POINTER_LOCK
// The protocol objects live on a queue of our own, so their events are
// dispatched here (pump), never inside Qt's own dispatching.
struct WaylandLock
{
  wl_display *display = nullptr;
  wl_event_queue *queue = nullptr;
  wl_display *wrapped = nullptr; // the display, its requests' new objects on our queue
  wl_registry *registry = nullptr;
  zwp_pointer_constraints_v1 *constraints = nullptr;
  zwp_relative_pointer_manager_v1 *relatives = nullptr;
  zwp_locked_pointer_v1 *locked = nullptr;
  zwp_relative_pointer_v1 *relative = nullptr;
  double dx = 0, dy = 0;
  bool lost = false;
};

namespace {

void onGlobal(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t)
{
  auto *w = static_cast<WaylandLock *>(data);
  if (!std::strcmp(iface, zwp_pointer_constraints_v1_interface.name))
    w->constraints = static_cast<zwp_pointer_constraints_v1 *>(
      wl_registry_bind(reg, name, &zwp_pointer_constraints_v1_interface, 1));
  else if (!std::strcmp(iface, zwp_relative_pointer_manager_v1_interface.name))
    w->relatives = static_cast<zwp_relative_pointer_manager_v1 *>(
      wl_registry_bind(reg, name, &zwp_relative_pointer_manager_v1_interface, 1));
}
void onGlobalGone(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener = {onGlobal, onGlobalGone};

void onLocked(void *, zwp_locked_pointer_v1 *) {}
void onUnlocked(void *data, zwp_locked_pointer_v1 *)
{
  static_cast<WaylandLock *>(data)->lost = true; // another window, a workspace: it is over
}
const zwp_locked_pointer_v1_listener lockedListener = {onLocked, onUnlocked};

void onRelative(void *data, zwp_relative_pointer_v1 *, uint32_t, uint32_t, wl_fixed_t, wl_fixed_t,
                wl_fixed_t dxRaw, wl_fixed_t dyRaw)
{
  auto *w = static_cast<WaylandLock *>(data);
  w->dx += wl_fixed_to_double(dxRaw); // unaccelerated: the hand's own movement, as games take it
  w->dy += wl_fixed_to_double(dyRaw);
}
const zwp_relative_pointer_v1_listener relativeListener = {onRelative};

WaylandLock *waylandLock()
{
  if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
    return nullptr;
  auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
  if (!app || !app->display())
    return nullptr;
  auto *w = new WaylandLock;
  w->display = app->display();
  w->queue = wl_display_create_queue(w->display);
  w->wrapped = static_cast<wl_display *>(wl_proxy_create_wrapper(w->display));
  wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(w->wrapped), w->queue);
  w->registry = wl_display_get_registry(w->wrapped);
  wl_registry_add_listener(w->registry, &registryListener, w);
  wl_display_roundtrip_queue(w->display, w->queue);
  if (!w->constraints || !w->relatives) {
    if (w->constraints)
      zwp_pointer_constraints_v1_destroy(w->constraints);
    if (w->relatives)
      zwp_relative_pointer_manager_v1_destroy(w->relatives);
    wl_registry_destroy(w->registry);
    wl_proxy_wrapper_destroy(w->wrapped);
    wl_event_queue_destroy(w->queue);
    delete w;
    return nullptr;
  }
  return w;
}

void waylandRelease(WaylandLock *w)
{
  if (w->locked)
    zwp_locked_pointer_v1_destroy(w->locked);
  if (w->relative)
    zwp_relative_pointer_v1_destroy(w->relative);
  w->locked = nullptr;
  w->relative = nullptr;
  w->dx = w->dy = 0;
  w->lost = false;
  wl_display_flush(w->display);
}

} // namespace
#else
struct WaylandLock
{
};
#endif

PointerLock::PointerLock(QWidget *widget) : QObject(widget), m_widget(widget)
{
#ifdef SYN_HAVE_POINTER_LOCK
  m_wl = waylandLock();
#endif
}

PointerLock::~PointerLock()
{
  unlock();
#ifdef SYN_HAVE_POINTER_LOCK
  if (m_wl) {
    zwp_pointer_constraints_v1_destroy(m_wl->constraints);
    zwp_relative_pointer_manager_v1_destroy(m_wl->relatives);
    wl_registry_destroy(m_wl->registry);
    wl_proxy_wrapper_destroy(m_wl->wrapped);
    wl_event_queue_destroy(m_wl->queue);
    delete m_wl;
  }
#endif
}

bool PointerLock::lock()
{
  if (m_locked)
    return true;
#ifdef SYN_HAVE_POINTER_LOCK
  if (m_wl) {
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    QWindow *win = m_widget->window()->windowHandle();
    auto *surface = win ? static_cast<wl_surface *>(
                      QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", win))
                        : nullptr;
    wl_pointer *pointer = app ? app->pointer() : nullptr;
    if (!surface || !pointer)
      return false;
    m_wl->relative = zwp_relative_pointer_manager_v1_get_relative_pointer(m_wl->relatives, pointer);
    zwp_relative_pointer_v1_add_listener(m_wl->relative, &relativeListener, m_wl);
    m_wl->locked = zwp_pointer_constraints_v1_lock_pointer(m_wl->constraints, surface, pointer, nullptr,
                                                           ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
    zwp_locked_pointer_v1_add_listener(m_wl->locked, &lockedListener, m_wl);
    wl_display_flush(m_wl->display);
    m_locked = true;
    m_widget->setCursor(Qt::BlankCursor);
    return true;
  }
#endif
  if (QGuiApplication::platformName() == QLatin1String("xcb")) {
    m_warp = true;
    m_locked = true;
    m_centre = m_widget->mapToGlobal(m_widget->rect().center());
    m_lastPos = m_centre;
    QCursor::setPos(m_centre);
    m_widget->setCursor(Qt::BlankCursor);
    m_widget->grabMouse();
    m_widget->installEventFilter(this);
    return true;
  }
  return false;
}

void PointerLock::unlock()
{
  if (!m_locked)
    return;
  m_locked = false;
#ifdef SYN_HAVE_POINTER_LOCK
  if (m_wl)
    waylandRelease(m_wl);
#endif
  if (m_warp) {
    m_warp = false;
    m_widget->removeEventFilter(this);
    m_widget->releaseMouse();
  }
  m_dx = m_dy = 0;
  m_widget->unsetCursor();
}

void PointerLock::pump()
{
  if (!m_locked)
    return;
  double dx = m_dx, dy = m_dy;
  m_dx = m_dy = 0;
#ifdef SYN_HAVE_POINTER_LOCK
  if (m_wl) {
    wl_display_dispatch_queue_pending(m_wl->display, m_wl->queue);
    dx += m_wl->dx;
    dy += m_wl->dy;
    m_wl->dx = m_wl->dy = 0;
    if (m_wl->lost) {
      unlock();
      emit lost();
      return;
    }
  }
#endif
  if (dx != 0 || dy != 0)
    emit moved(dx, dy);
}

// X11: each move is how far from the one before; the pointer is sent back
// to the middle once it has gone a way off, and the move that brings it
// there is not counted (moves already queued still measure from the last).
bool PointerLock::eventFilter(QObject *o, QEvent *e)
{
  if (m_warp && o == m_widget && e->type() == QEvent::MouseMove) {
    const QPoint at = static_cast<QMouseEvent *>(e)->globalPosition().toPoint();
    if (at == m_centre && m_lastPos != m_centre) {
      m_lastPos = at; // the warp arriving
      return true;
    }
    m_dx += at.x() - m_lastPos.x();
    m_dy += at.y() - m_lastPos.y();
    m_lastPos = at;
    const QPoint off = at - m_centre;
    if (qAbs(off.x()) > m_widget->width() / 4 || qAbs(off.y()) > m_widget->height() / 4)
      QCursor::setPos(m_centre);
    return true;
  }
  return QObject::eventFilter(o, e);
}
