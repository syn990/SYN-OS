// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   PointerLock: the window holds the mouse, as a game does, for walking
//   in the space view. The pointer stays put and hidden, and what the mouse
//   does comes as movement (moved), not as a place, with no screen edge to
//   run into.
//
//   On Wayland through the compositor's pointer-constraints and
//   relative-pointer protocols (labwc, sway and the rest of wlroots, KWin,
//   Mutter), their events on a queue of our own beside Qt's, delivered by
//   pump(). On X11 by putting the pointer back in the middle of the widget
//   after each move. Where neither works lock() says so, and the caller
//   keeps to the plain mouse.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QObject>
#include <QPoint>

class QWidget;
struct WaylandLock;

class PointerLock : public QObject
{
  Q_OBJECT

public:
  explicit PointerLock(QWidget *widget);
  ~PointerLock() override;

  bool lock(); // false: it can't be held here
  void unlock();
  bool isLocked() const { return m_locked; }
  // Hands on what the mouse did since last time (moved); call each frame.
  void pump();

signals:
  void moved(double dx, double dy); // in the mouse's own counts
  void lost();                      // let go from outside (the compositor, the window)

protected:
  bool eventFilter(QObject *o, QEvent *e) override;

private:
  QWidget *m_widget;
  WaylandLock *m_wl = nullptr; // null: not Wayland, or it has no such protocols
  bool m_locked = false;
  bool m_warp = false; // X11: the pointer put back in the middle
  QPoint m_centre, m_lastPos;
  double m_dx = 0, m_dy = 0;
};
