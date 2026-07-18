// ------------------------------------------------------------------------------
//                     S Y N - F I L E M A N A G E R
//
//   Entry point: builds the app and shows one MainWindow rooted at $HOME.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-FILEMANAGER (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include <QApplication>
#include <QDir>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);
  app.setApplicationName("syn-filemanager");
  app.setOrganizationName("SYN-OS");

  QString startPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                : QDir::homePath();

  MainWindow window(startPath);
  window.resize(1100, 700);
  window.show();

  return app.exec();
}
