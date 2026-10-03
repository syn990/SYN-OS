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

#include "MainWindow.h"

int main(int argc, char *argv[])
{
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
