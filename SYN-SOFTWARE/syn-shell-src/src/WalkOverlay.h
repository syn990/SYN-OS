// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   WalkOverlay: what E does to a file in the walk (SpaceView, walk mode).
//   The room's C side says which file was used (SpaceView::used); this
//   does it: plays audio in place through ffplay (run, never linked; one
//   track at a time, stopped on leaving the walk or quitting), shows a
//   picture large or a text file's text over the room, tells what a
//   program or a device in /dev is (programs are never run), and hands
//   anything else to its usual app. Archives are MainWindow's: it opens
//   them in the browser.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QPlainTextEdit;
class QProcess;
class SpaceView;

// Over the walk view, the whole of it: a picture, a file's text or a
// few lines about a thing. E or Esc closes it and the room has the mouse
// again; while it is open the keys are its own.
class WalkOverlay : public QWidget
{
  Q_OBJECT

public:
  explicit WalkOverlay(SpaceView *view);

  bool showPicture(const QString &path);
  void showText(const QString &path);
  // title, then "label: value" lines; openable: Enter hands it to its app
  void showInfo(const QString &path, const QString &title, const QStringList &lines, bool openable);
  void close(bool recapture = true);

protected:
  void paintEvent(QPaintEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  bool eventFilter(QObject *o, QEvent *e) override;

private:
  enum Mode { Picture, Text, Info };
  void open(Mode mode, const QString &path, bool openable);
  QRect panel() const;
  bool key(QKeyEvent *e); // true if it was the overlay's

  SpaceView *m_view;
  Mode m_mode = Info;
  QString m_path, m_title, m_note;
  QImage m_image;
  QStringList m_lines;
  bool m_openable = false;
  QPlainTextEdit *m_text;
};

// Does what E asks, by the file's kind.
class WalkUse : public QObject
{
  Q_OBJECT

public:
  explicit WalkUse(SpaceView *view);
  ~WalkUse() override;

  void use(const QString &path, int kind, int dev);
  void stop(); // the music, if any
  bool overlayOpen() const;

private:
  void play(const QString &path);
  void setPlaying(const QString &path);
  void openElsewhere(const QString &path);
  void deviceInfo(const QString &path, int dev);
  void fileInfo(const QString &path, bool program);

  SpaceView *m_view;
  WalkOverlay *m_overlay;
  QString m_ffplay, m_playing;
  QProcess *m_proc = nullptr;
  QTimer m_watch;
};
