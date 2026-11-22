// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   TestDrive: see TestDrive.h.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "TestDrive.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include "SpaceView.h"

bool TestDrive::take(int argc, char **argv, int &i)
{
  const QByteArray a(argv[i]);
  if (i + 1 >= argc)
    return false;
  QString *to = a == "--shot"         ? &m_shot
                : a == "--view-keys"  ? &m_keys
                : a == "--view-click" ? &m_click
                : a == "--view-drag"  ? &m_drag
                : a == "--size"       ? &m_size
                : a == "--view-script" ? &m_script
                                      : nullptr;
  if (to)
    *to = QString::fromLocal8Bit(argv[++i]);
  else if (a == "--shot-at")
    m_shotAt = atoi(argv[++i]);
  else
    return false;
  return true;
}

static QPoint pair(const QString &s, QChar sep)
{
  return QPoint(s.section(sep, 0, 0).toInt(), s.section(sep, 1, 1).toInt());
}

static void keyTo(QWidget *v, const QString &name, bool down)
{
  int key = 0;
  QString text;
  if (name == QLatin1String("space"))
    key = Qt::Key_Space, text = QStringLiteral(" ");
  else if (name == QLatin1String("shift"))
    key = Qt::Key_Shift;
  else if (name == QLatin1String("esc"))
    key = Qt::Key_Escape;
  else if (name == QLatin1String("enter"))
    key = Qt::Key_Return;
  else if (name == QLatin1String("left"))
    key = Qt::Key_Left;
  else if (name == QLatin1String("right"))
    key = Qt::Key_Right;
  else if (name.size() == 1)
    key = name.at(0).toUpper().unicode(), text = name;
  QKeyEvent e(down ? QEvent::KeyPress : QEvent::KeyRelease, key, Qt::NoModifier, text);
  QApplication::sendEvent(v, &e);
}

// One step of --view-script, then the next when it's done.
static void runScript(QWidget *window, QStringList steps, int i)
{
  SpaceView *v = window->findChild<SpaceView *>();
  if (!v || i >= steps.size())
    return;
  const QStringList p = steps.at(i).split(QLatin1Char(':'));
  int after = 50;
  const QString op = p.value(0);
  if (op == QLatin1String("click")) {
    const QPoint xy = pair(p.value(1), QLatin1Char(','));
    const QPointF at(xy.x() < 0 ? v->width() + xy.x() : xy.x(), xy.y() < 0 ? v->height() + xy.y() : xy.y());
    v->setFocus();
    QMouseEvent down(QEvent::MouseButtonPress, at, v->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent up(QEvent::MouseButtonRelease, at, v->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(v, &down);
    QApplication::sendEvent(v, &up);
    after = 300;
  } else if (op == QLatin1String("key")) {
    keyTo(v, p.value(1), true);
    keyTo(v, p.value(1), false);
  } else if (op == QLatin1String("hold")) {
    keyTo(v, p.value(1), true);
    const int ms = p.value(2).toInt();
    QTimer::singleShot(ms, v, [v, name = p.value(1)] { keyTo(v, name, false); });
    after = ms + 20;
  } else if (op == QLatin1String("down")) {
    keyTo(v, p.value(1), true); /* held till up:K, so two can be held at once */
  } else if (op == QLatin1String("up")) {
    keyTo(v, p.value(1), false);
  } else if (op == QLatin1String("wait")) {
    after = p.value(1).toInt();
  } else if (op == QLatin1String("look")) {
    const QPoint d = pair(p.value(1), QLatin1Char(','));
    space_look(v->app(), float(d.x()), float(d.y()));
  } else if (op == QLatin1String("goto")) {
    space_walk_to(v->app(), p.value(1).toUtf8().constData());
  } else if (op == QLatin1String("shot")) {
    window->grab().save(p.value(1));
  } else if (op == QLatin1String("report")) {
    char target[600] = "";
    space_target(v->app(), target, sizeof target);
    const QWidget *o = v->findChild<QWidget *>(QStringLiteral("walkOverlay"));
    fprintf(stderr, "test: walking %d, mouse held %d, target \"%s\", overlay %d\n", space_walking(v->app()),
            space_captured(v->app()), target, o && o->isVisible());
  }
  QTimer::singleShot(after, window, [window, steps, i] { runScript(window, steps, i + 1); });
}

void TestDrive::start(QWidget *window) const
{
  if (!m_size.isEmpty()) {
    const QPoint wh = pair(m_size, QLatin1Char('x'));
    window->resize(wh.x(), wh.y());
  }
  if (!m_keys.isEmpty() || !m_click.isEmpty() || !m_drag.isEmpty())
    QTimer::singleShot(1500, window, [window, keys = m_keys, click = m_click, drag = m_drag] {
      SpaceView *v = window->findChild<SpaceView *>();
      if (!v)
        return;
      v->setFocus();
      auto mouse = [v](QEvent::Type t, QPointF at, Qt::MouseButton b, Qt::MouseButtons held) {
        QMouseEvent e(t, at, v->mapToGlobal(at), b, held, Qt::NoModifier);
        QApplication::sendEvent(v, &e);
      };
      if (!click.isEmpty()) {
        const QPoint xy = pair(click, QLatin1Char(','));
        const QPointF at(xy.x() < 0 ? v->width() + xy.x() : xy.x(), xy.y() < 0 ? v->height() + xy.y() : xy.y());
        mouse(QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
      }
      if (!drag.isEmpty()) {
        const QPointF from(v->width() / 2.0, v->height() / 2.0);
        const QPointF to = from + QPointF(pair(drag, QLatin1Char(',')));
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int s = 1; s <= 20; ++s)
          mouse(QEvent::MouseMove, from + (to - from) * (s / 20.0), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
      }
      for (QChar c : keys) {
        const int key = c == QLatin1Char(' ') ? Qt::Key_Space : c == QLatin1Char('\t') ? Qt::Key_Tab : c.toUpper().unicode();
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, QString(c));
        QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, QString(c));
        QApplication::sendEvent(v, &down);
        QApplication::sendEvent(v, &up);
      }
    });
  if (!m_script.isEmpty())
    QTimer::singleShot(1700, window, [window, steps = m_script.split(QLatin1Char(';'), Qt::SkipEmptyParts)] {
      runScript(window, steps, 0);
    });
  if (!m_shot.isEmpty())
    QTimer::singleShot(m_shotAt, window, [window, shot = m_shot] {
      QApplication::exit(window->grab().save(shot) ? 0 : 1);
    });
}
