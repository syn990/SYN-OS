// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Entry point: builds the app and shows one MainWindow, opened on the
//   path given (a folder, a file's folder, or an archive), else $HOME.
//   With --space it is the disk space window
//   instead (src/space/).
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

#include <cstdlib>
#include <cstring>

#ifdef SYN_SHELL_TEST
#include "TestDrive.h"
#endif
#include "MainWindow.h"
#include "PtySession.h"
#ifdef Q_OS_WIN
#include "Win.h"

#include <QFileSystemWatcher>
#include <QStyleFactory>
#include <QTimer>
#endif

namespace {

// English plurals with no translation files: Qt hands every tr() with a
// count to the installed translators first, so "%n item(s)" becomes
// "1 item" or "3 items" here, before Qt puts the number in.
class EnglishPlurals : public QTranslator
{
public:
  bool isEmpty() const override { return false; }
  QString translate(const char *, const char *source, const char *, int n) const override
  {
    if (n < 0 || !source || !strstr(source, "(s)"))
      return QString();
    QString text = QString::fromUtf8(source);
    text.replace(QStringLiteral("(s)"), n == 1 ? QString() : QStringLiteral("s"));
    return text;
  }
};

} // namespace

#ifndef Q_OS_WIN
// Disk space (src/space/): its own SDL + OpenGL
// window, run in place of the Qt one. Plain C.
extern "C" int space_main(int argc, char **argv);
#endif

int main(int argc, char *argv[])
{
  // The binary's two other jobs (see PtySession.h), before any GUI:
  //   --pty-hold SOCKET CWD ROWS COLS   hold a shell for the terminal area
  //   --attach SOCKET                   take that shell over, inside foot
  //   --pty-hold SOCKET CWD ROWS COLS -- PROGRAM ARGS   the same, a program
  if (argc >= 6 && QByteArray(argv[1]) == "--pty-hold") {
    QStringList command;
    for (int i = 7; argc > 7 && QByteArray(argv[6]) == "--" && i < argc; ++i)
      command << QString::fromLocal8Bit(argv[i]);
    return PtySession::runHolder(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]),
                                 atoi(argv[4]), atoi(argv[5]), command);
  }
  if (argc == 3 && QByteArray(argv[1]) == "--attach")
    return PtySession::runAttach(QString::fromLocal8Bit(argv[2]));
#ifndef Q_OS_WIN
  //   --space [OPTIONS] [PATH]          where the space has gone (see --space --help)
  {
    if (argc >= 2 && !strcmp(argv[1], "--space")) {
      argv[1] = argv[0];
      return space_main(argc - 1, argv + 1);
    }
  }
#endif

#ifndef Q_OS_WIN
  // The space view (SpaceView) draws with OpenGL 3.3 core, which Mesa only
  // gives a core profile; set before the app so every context agrees.
  {
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);
  }
#endif

  QApplication app(argc, argv);
  app.setApplicationName("syn-shell");
  app.setOrganizationName("SYN-OS");
  EnglishPlurals plurals;
  app.installTranslator(&plurals);
#ifdef Q_OS_WIN

  // What qt6ct sets on SYN-OS: Fusion, Terminus, the active SYN theme's
  // palette, here from SYN-WIN (see Win.h), again on a theme
  // switch, which the window takes as qt6ct's (a palette change).
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  if (Win::hasTerminus())
    app.setFont(Win::uiFont());
  auto applyTheme = [] {
    const QHash<QString, QColor> theme = Win::themeColours();
    if (!theme.isEmpty())
      QApplication::setPalette(Win::palette(theme));
  };
  applyTheme();
  auto *themeWatch = new QFileSystemWatcher(Win::themeFiles(), &app);
  auto *themeSettle = new QTimer(&app);
  themeSettle->setSingleShot(true);
  themeSettle->setInterval(200); // a switch rewrites several files
  QObject::connect(themeWatch, &QFileSystemWatcher::fileChanged, themeSettle, [themeWatch, themeSettle] {
    // A file rewritten by rename leaves the watch: put it back.
    themeWatch->addPaths(Win::themeFiles());
    themeSettle->start();
  });
  QObject::connect(themeSettle, &QTimer::timeout, &app, applyTheme);
#endif

  // Settings kept under the old name (window size, columns, hidden
  // files) carry over once.
  QSettings settings;
  if (settings.allKeys().isEmpty()) {
    QSettings old(QStringLiteral("SYN-OS"), QStringLiteral("syn-filemanager"));
    for (const QString &key : old.allKeys())
      settings.setValue(key, old.value(key));
  }

  // syn-shell [PATH] [--tree] [--view]: start with the space tree / view on.
  QString startPath = QDir::homePath();
  bool tree = false, view = false;
#ifdef SYN_SHELL_TEST
  TestDrive drive; // the test build's --shot / --view-* options
#endif
  for (int i = 1; i < argc; ++i) {
    const QByteArray a(argv[i]);
    if (a == "--tree")
      tree = true;
    else if (a == "--view")
      view = true;
#ifdef SYN_SHELL_TEST
    else if (drive.take(argc, argv, i))
      continue;
#endif
    else
      startPath = QString::fromLocal8Bit(argv[i]);
  }

  MainWindow window(startPath);
#ifndef Q_OS_WIN
  if (tree)
    window.setSpaceTree(true);
  if (view)
    window.setSpaceView(true);
#else
  Q_UNUSED(tree);
  Q_UNUSED(view);
#endif
  window.show();
#ifdef SYN_SHELL_TEST
  drive.start(&window);
#endif
#ifdef Q_OS_WIN
  Win::seeThrough(window.winId()); // so the opacity setting reaches the desktop
#endif

  return app.exec();
}
