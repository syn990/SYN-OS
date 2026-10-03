// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   TermView: the terminal area. libvterm does the emulation (xterm-256
//   colours, alternate screen, bracketed paste, mouse reporting, reflow);
//   this widget draws its screen in the fixed-width font with foot's own
//   colours (read from ~/.config/foot/foot.ini, which the SYN theme
//   writes), keeps 10000 lines of scrollback, selects with the mouse and
//   copies and pastes. The shell itself lives in a PtySession holder this
//   widget talks to over a local socket, which is what lets it go to foot.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QTimer>
#include <QVector>
#include <QWidget>

class QVariantAnimation;

extern "C" {
#include <vterm.h>
}

class QLocalSocket;

class TermView : public QWidget
{
  Q_OBJECT

public:
  explicit TermView(QWidget *parent = nullptr);
  ~TermView() override;

  bool isRunning() const { return m_running || m_connectTimer.isActive(); }
  // Starts a shell in `cwd` (no-op if one is running).
  void start(const QString &cwd);
  // Ask the shell to cd: done only if it's idle at a zsh prompt.
  void requestCd(const QString &dir);
  // Hands the running shell to a new foot window.
  bool detachToFoot(QString *error);
  QString cwd() const { return m_cwd; }
  void reloadColours();
  // Text size in pixels; animated steps along Zoom::kLadder when asked.
  int fontPixels() const { return m_fontPx; }
  void setFontPixels(int px, bool animate);
  void zoomBy(int steps);

signals:
  void cwdChanged(const QString &dir);
  // detached: true when the shell went to foot, false when it ended.
  void finished(bool detached);
  void toggleFocusRequested();
  void detachRequested();
  void hideRequested();
  void zoomChanged(int px);

protected:
  bool event(QEvent *e) override;
  void paintEvent(QPaintEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
  bool focusNextPrevChild(bool) override { return false; } // Tab belongs to the shell

private:
  // libvterm callbacks
  static int cbDamage(VTermRect rect, void *user);
  static int cbMoveRect(VTermRect dest, VTermRect src, void *user);
  static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
  static int cbSetTermProp(VTermProp prop, VTermValue *val, void *user);
  static int cbBell(void *user);
  static int cbPushLine(int cols, const VTermScreenCell *cells, void *user);
  static int cbPopLine(int cols, VTermScreenCell *cells, void *user);
  static int cbClearScrollback(void *user);
  static int cbOsc(int command, VTermStringFragment frag, void *user);
  static void cbOutput(const char *s, size_t len, void *user);

  void send(char type, const QByteArray &payload);
  void tryConnect();
  void onReadyRead();
  void onDisconnected();
  void updateGeometry();
  void resetTerminal();
  void applyFont(int px);
  void resizeGrid();

  // Absolute line numbers count scrollback first (0 = oldest), then the
  // screen; the view shows m_rows of them ending m_scrollOffset above
  // the bottom.
  int firstVisibleLine() const;
  VTermScreenCell cellAt(int line, int col) const;
  QColor colour(VTermColor c, bool foreground) const;
  QPoint cellAtPixel(const QPoint &p) const; // (col, absolute line)
  QString selectedText() const;
  bool isSelected(int line, int col) const;
  void pasteText(const QString &text);
  void scrollBy(int lines);
  VTermModifier modifiers(Qt::KeyboardModifiers m) const;
  bool forwardMouse() const;

  VTerm *m_vt;
  VTermScreen *m_screen;
  QLocalSocket *m_sock;
  QString m_sockPath;
  QTimer m_connectTimer;
  int m_connectTries = 0;
  bool m_running = false;
  bool m_detaching = false;
  QString m_cwd;

  QFont m_font;
  int m_fontPx = 12;
  // While a zoom animates, the grid is drawn scaled instead of re-laid
  // out every frame: the shell gets one resize, at the end.
  qreal m_paintScale = 1.0;
  QVariantAnimation *m_zoomAnim;
  int m_wheelAcc = 0;
  QTimer m_zoomTag; // shows the new size for a moment
  int m_cw = 8, m_ch = 16, m_ascent = 12;
  int m_rows = 24, m_cols = 80;

  QList<QVector<VTermScreenCell>> m_scrollback;
  int m_scrollOffset = 0;

  VTermPos m_cursor{0, 0};
  bool m_cursorVisible = true;
  int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
  bool m_altScreen = false;
  int m_mouseMode = VTERM_PROP_MOUSE_NONE;
  QByteArray m_osc;

  bool m_selecting = false;
  bool m_hasSelection = false;
  QPoint m_selAnchor, m_selEnd; // (col, absolute line)

  QColor m_fg, m_bg, m_palette[16];
};
