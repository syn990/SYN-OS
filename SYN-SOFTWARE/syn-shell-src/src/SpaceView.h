// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   SpaceView: the right-hand column's other face (zv). Where the space
//   has gone under the folder the browser is in, drawn by src/space/ (what
//   src/space/): the block map or the node graph, flat or solid and
//   turning, and the walk, the folder as a room with its biggest folders
//   as doorways. The scan runs in its own thread and the picture grows as
//   it goes.
//
//   It follows the browser (showDir, selectPath) and the browser follows
//   it: going into a folder here, up, or through a doorway while walking
//   moves the browser there (focusRequested). The scan's tree is also what
//   SpaceTree shows in the left-hand column (zt), so this owns it even
//   while it is not shown itself.
//
//   It is also the corner render (setMini): small in the bottom right of
//   the window while you browse, turning, made big by a click.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QOpenGLWidget>

class PointerLock;
#include <QSet>
#include <QTimer>

extern "C" {
#define SDL_MAIN_HANDLED
#include "space/app.h"
#include "space/walk.h"
}

class SpaceView : public QOpenGLWidget
{
  Q_OBJECT

public:
  explicit SpaceView(QWidget *parent = nullptr);
  ~SpaceView() override;

  App *app() const { return m_app; }
  void showDir(const QString &dir);
  void selectPath(const QString &path);
  // Whether the tree or the view is in use: the scan's news is fetched
  // while it is, shown or not.
  void setActive(bool on);
  // The corner render: the view alone, turning, a click on it asks to be
  // made big (enlargeRequested) and nothing else reaches it.
  void setMini(bool on);
  bool isMini() const { return m_mini; }

signals:
  void focusRequested(const QString &dir); // the user went to this folder here
  void selected(const QString &path);      // the user picked this here
  void treeDropped();                      // the tree is about to be freed: let go of it now
  void treeChanged();                      // a new tree, or the live one grew
  void escaped();                          // Esc with nothing here to stop: back to the files
  void enlargeRequested();                 // the corner render was clicked
  void used(const QString &path, int kind, int dev); // E on a file while walking: kinds.c's kind, a WALK_DEV_

protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void keyReleaseEvent(QKeyEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;
  bool focusNextPrevChild(bool) override { return false; } // Tab is the view's

private:
  static void dropped(void *self);
  void send(const SDL_Event &e);
  void poll();
  void tick();
  bool key(QKeyEvent *e, bool down);
  void syncLock(); // the pointer held while the room has the mouse, and only then

  App *m_app;
  QTimer m_timer;
  bool m_active = false;
  bool m_gl = false;
  int m_clicks = 1;
  QPointF m_last;
  double m_wheel = 0;
  unsigned m_gen = ~0u;
  bool m_wasLive = false;
  bool m_mini = false;
  PointerLock *m_lock;
  QSet<int> m_held; // scancodes down
};
