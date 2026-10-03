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
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

#include <cstdlib>
#include <cstring>

#include "MainWindow.h"
#include "PtySession.h"

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

  QApplication app(argc, argv);
  app.setApplicationName("syn-shell");
  app.setOrganizationName("SYN-OS");
  EnglishPlurals plurals;
  app.installTranslator(&plurals);

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
