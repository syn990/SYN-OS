// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Entry point: builds the app and shows one MainWindow, opened on the
//   path given (a folder, a file's folder, or an archive), else $HOME.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include <QApplication>
#include <QDir>
#include <QSettings>

#include <cstdlib>

#include "MainWindow.h"
#include "PtySession.h"

int main(int argc, char *argv[])
{
  // The binary's two other jobs (see PtySession.h), before any GUI:
  //   --pty-hold SOCKET CWD ROWS COLS   hold a shell for the terminal area
  //   --attach SOCKET                   take that shell over, inside foot
  if (argc == 6 && QByteArray(argv[1]) == "--pty-hold")
    return PtySession::runHolder(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]),
                                 atoi(argv[4]), atoi(argv[5]));
  if (argc == 3 && QByteArray(argv[1]) == "--attach")
    return PtySession::runAttach(QString::fromLocal8Bit(argv[2]));

  QApplication app(argc, argv);
  app.setApplicationName("syn-shell");
  app.setOrganizationName("SYN-OS");

  // Settings kept under the old name (window size, columns, hidden
  // files) carry over once.
  QSettings settings;
  if (settings.allKeys().isEmpty()) {
    QSettings old(QStringLiteral("SYN-OS"), QStringLiteral("syn-filemanager"));
    for (const QString &key : old.allKeys())
      settings.setValue(key, old.value(key));
  }

  QString startPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                : QDir::homePath();

  MainWindow window(startPath);
  window.show();

  return app.exec();
}
