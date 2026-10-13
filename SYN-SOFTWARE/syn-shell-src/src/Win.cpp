#include "Win.h"
#include "PtySession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

#include <cstring>

#include <windows.h>
#include <dwmapi.h>
#include <oleidl.h>
#include <sddl.h>

// syn-bar-core plays SYN-OS's sounds over a unix socket; there's no
// syn-bar on Windows, so asking for one does nothing (FileOps.cpp).
extern "C" void syn_bar_notify_meaning(const char *)
{
}

namespace Win {

namespace {

// The folder holding .state\current-theme and themes\: %SYN_WIN_SHELL%
// if it's set, else SYN-WIN's shell (what SYN-OS-WINDOWS became), else
// SYN-OS-WINDOWS itself; "" if there's none.
QString themeRoot()
{
  const QString env = qEnvironmentVariable("SYN_WIN_SHELL");
  if (!env.isEmpty())
    return QDir::fromNativeSeparators(env);
  for (const QString &dir : {QDir::homePath() + QStringLiteral("/SYN-WIN/shell"),
                             QDir::homePath() + QStringLiteral("/SYN-OS-WINDOWS")})
    if (QFileInfo::exists(dir + QStringLiteral("/.state/current-theme")))
      return dir;
  return QString();
}

// A text file as written by Windows PowerShell's Set-Content -Encoding
// utf8, which starts it with a byte-order mark.
QString readText(QFile &f)
{
  QString text = QString::fromUtf8(f.readAll());
  if (text.startsWith(QChar(0xFEFF)))
    text.remove(0, 1);
  return text;
}

} // namespace

QByteArray dropEffect(bool cut)
{
  const quint32 effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY | DROPEFFECT_LINK;
  return QByteArray(reinterpret_cast<const char *>(&effect), 4);
}

bool isCut(const QByteArray &dropEffect)
{
  if (dropEffect.size() < 4)
    return false;
  quint32 effect;
  memcpy(&effect, dropEffect.constData(), 4);
  return effect & DROPEFFECT_MOVE;
}

QString errorText(unsigned long error)
{
  wchar_t *buf = nullptr;
  const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, error, 0, reinterpret_cast<wchar_t *>(&buf), 0, nullptr);
  QString text = n ? QString::fromWCharArray(buf, int(n)).trimmed() : QStringLiteral("error %1").arg(error);
  LocalFree(buf);
  if (text.endsWith(QLatin1Char('.')))
    text.chop(1);
  return text;
}

QString recycleBin(const QString &drive)
{
  static const QString sid = [] {
    QString out;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      return out;
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    QByteArray buf(int(len), 0);
    wchar_t *str = nullptr;
    if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)
        && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid, &str)) {
      out = QString::fromWCharArray(str);
      LocalFree(str);
    }
    CloseHandle(token);
    return out;
  }();
  return drive.left(2).toUpper() + QStringLiteral("/$Recycle.Bin/") + sid;
}

QStringList themeFiles()
{
  const QString root = themeRoot();
  QStringList files;
  if (root.isEmpty())
    return files;
  if (QFileInfo::exists(root + QStringLiteral("/.state/current-theme")))
    files << root + QStringLiteral("/.state/current-theme");
  for (const QString &t : QDir(root + QStringLiteral("/themes")).entryList({QStringLiteral("*.theme")}))
    files << root + QStringLiteral("/themes/") + t;
  return files;
}

QHash<QString, QColor> themeColours()
{
  QHash<QString, QColor> out;
  const QString root = themeRoot();
  QFile current(root + QStringLiteral("/.state/current-theme"));
  if (root.isEmpty() || !current.open(QIODevice::ReadOnly | QIODevice::Text))
    return out;
  const QString name = readText(current).trimmed();
  QFile theme(root + QStringLiteral("/themes/") + name + QStringLiteral(".theme"));
  if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
      || !theme.open(QIODevice::ReadOnly | QIODevice::Text))
    return out;
  // KEY="#rrggbb" lines, the same file SYN-OS's themes are.
  for (QString line : readText(theme).split(QLatin1Char('\n'))) {
    line = line.trimmed();
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq <= 0 || line.startsWith(QLatin1Char('#')))
      continue;
    QString value = line.mid(eq + 1).trimmed();
    if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
      value = value.mid(1, value.size() - 2);
    const QColor c(value);
    if (value.startsWith(QLatin1Char('#')) && c.isValid())
      out.insert(line.left(eq).trimmed(), c);
  }
  return out;
}

QPalette palette(const QHash<QString, QColor> &theme)
{
  // qt6ct-colors.conf.tmpl, role by role in QPalette::ColorRole order
  // (WindowText .. PlaceholderText); the last is the accent at half alpha.
  static const char *const active[] = {
    "SYN_TEXT", "SYN_PANEL", "SYN_PANEL_HOVER", "SYN_PANEL", "SYN_BG_ALT", "SYN_BORDER",
    "SYN_TEXT", "SYN_TEXT", "SYN_TEXT", "SYN_BG_ALT", "SYN_BG", "SYN_BG_ALT",
    "SYN_ACCENT", "SYN_TEXT", "SYN_ACCENT", "SYN_ACCENT_DIM", "SYN_BG_ALT", "SYN_TEXT",
    "SYN_BG_ALT", "SYN_TEXT", "SYN_ACCENT",
  };
  static const char *const disabled[] = {
    "SYN_BORDER", "SYN_PANEL", "SYN_PANEL_HOVER", "SYN_PANEL", "SYN_BG_ALT", "SYN_BORDER",
    "SYN_BORDER", "SYN_BORDER", "SYN_BORDER", "SYN_BG_ALT", "SYN_BG", "SYN_BG_ALT",
    "SYN_ACCENT_DIM", "SYN_BORDER", "SYN_ACCENT_DIM", "SYN_ACCENT_DIM", "SYN_BG_ALT", "SYN_BORDER",
    "SYN_BG_ALT", "SYN_BORDER", "SYN_ACCENT",
  };
  QPalette pal;
  for (int r = 0; r < 21; ++r) {
    const auto role = QPalette::ColorRole(r);
    if (role == QPalette::NoRole)
      continue;
    QColor a = theme.value(QLatin1String(active[r]));
    QColor d = theme.value(QLatin1String(disabled[r]));
    if (role == QPalette::PlaceholderText) {
      a.setAlpha(0x80);
      d.setAlpha(0x80);
    }
    pal.setColor(QPalette::Active, role, a);
    pal.setColor(QPalette::Inactive, role, a);
    pal.setColor(QPalette::Disabled, role, d);
  }
  // A 21-colour scheme has no accent of its own: it's the highlight.
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
    pal.setColor(group, QPalette::Accent, pal.color(group, QPalette::Highlight));
  return pal;
}

QHash<QString, QColor> terminalColours(const QHash<QString, QColor> &theme)
{
  // foot-colors-dark.tmpl
  static const char *const regular[] = {"SYN_BG_ALT", "SYN_ACCENT", "SYN_TEXT", "SYN_PANEL_HOVER",
                                        "SYN_BORDER", "SYN_ACCENT_DIM", "SYN_PANEL", "SYN_TEXT"};
  static const char *const bright[] = {"SYN_BORDER", "SYN_URGENT", "SYN_TEXT", "SYN_PANEL_HOVER",
                                       "SYN_BORDER", "SYN_ACCENT_DIM", "SYN_PANEL", "SYN_TEXT"};
  QHash<QString, QColor> out;
  if (theme.isEmpty())
    return out;
  out.insert(QStringLiteral("background"), theme.value(QStringLiteral("SYN_BG")));
  out.insert(QStringLiteral("foreground"), theme.value(QStringLiteral("SYN_TEXT")));
  for (int i = 0; i < 8; ++i) {
    out.insert(QStringLiteral("regular%1").arg(i), theme.value(QLatin1String(regular[i])));
    out.insert(QStringLiteral("bright%1").arg(i), theme.value(QLatin1String(bright[i])));
  }
  return out;
}

void seeThrough(quintptr winId)
{
  // Blur-behind over a region that's empty: Windows takes the window's
  // alpha as it is and blurs nothing (what Qt does for its own see-through
  // OpenGL windows). The frame's glass instead (DwmExtendFrameIntoClientArea)
  // shows Windows' accent colour through it on Windows 11, not the desktop.
  const HRGN nothing = CreateRectRgn(0, 0, -1, -1);
  DWM_BLURBEHIND blur{};
  blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
  blur.fEnable = TRUE;
  blur.hRgnBlur = nothing;
  DwmEnableBlurBehindWindow(reinterpret_cast<HWND>(winId), &blur);
  DeleteObject(nothing);
}

bool hasTerminus()
{
  return QFontDatabase::hasFamily(QStringLiteral("Terminess Nerd Font"))
      && QFontDatabase::hasFamily(QStringLiteral("Terminess Nerd Font Mono"));
}

QFont uiFont()
{
  // qt6ct.conf: general="Terminus,9"
  return QFont(QStringLiteral("Terminess Nerd Font"), 9);
}

QFont fixedFont()
{
  if (!hasTerminus())
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
  // qt6ct.conf: fixed="Terminus,9"
  QFont f(QStringLiteral("Terminess Nerd Font Mono"), 9);
  f.setStyleHint(QFont::Monospace);
  return f;
}

bool openTerminal(const QString &dir, const QString &command, QString *error)
{
  QStringList run; // the shell running `command`; empty for the terminal's own default
  if (!command.isEmpty()) {
    const QString shell = PtySession::shellProgram();
    run << QDir::toNativeSeparators(shell);
    if (PtySession::isPowerShell(shell))
      run << QStringLiteral("-NoExit") << QStringLiteral("-Command") << command;
    else
      run << QStringLiteral("/k") << command;
  }
  const QString wt = QStandardPaths::findExecutable(QStringLiteral("wt"));
  if (!wt.isEmpty()) {
    // Its own window, as foot opens one; wt reads a bare ; as the start of
    // another command of its own.
    QStringList args{QStringLiteral("-w"), QStringLiteral("new"), QStringLiteral("-d"),
                     QDir::toNativeSeparators(dir)};
    for (const QString &a : run)
      args << QString(a).replace(QLatin1Char(';'), QLatin1String("\\;"));
    if (QProcess::startDetached(wt, args))
      return true;
  }
  if (run.isEmpty())
    run << QDir::toNativeSeparators(PtySession::shellProgram());
  QProcess proc;
  proc.setProgram(run.first());
  proc.setArguments(run.mid(1));
  proc.setWorkingDirectory(dir);
  proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
    a->flags |= CREATE_NEW_CONSOLE;
  });
  if (proc.startDetached())
    return true;
  *error = QObject::tr("could not start Windows Terminal or %1").arg(QFileInfo(run.first()).fileName());
  return false;
}

QStringList drives()
{
  QStringList roots;
  for (const QStorageInfo &v : QStorageInfo::mountedVolumes())
    if (v.isValid() && v.isReady())
      roots << v.rootPath();
  roots.sort(Qt::CaseInsensitive);
  return roots;
}

QString driveLabel(const QString &root)
{
  const QString name = QStorageInfo(root).name();
  return root.left(2) + (name.isEmpty() ? QString() : QStringLiteral("  ") + name);
}

} // namespace Win
