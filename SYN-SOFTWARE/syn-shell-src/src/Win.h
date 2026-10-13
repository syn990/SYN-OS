// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Win: what SYN-SHELL needs from Windows that Qt doesn't give, in one
//   place (only built there). The SYN theme, which on Linux arrives
//   through qt6ct and foot.ini, comes from SYN-WIN's shell here (or the
//   SYN-OS-WINDOWS it grew out of): its .state\current-theme names a
//   themes\*.theme, mapped to the palette and the terminal's colours
//   exactly as SYN-OS's qt6ct and foot templates map it. Found through
//   %SYN_WIN_SHELL%, else %USERPROFILE%\SYN-WIN\shell, else
//   %USERPROFILE%\SYN-OS-WINDOWS; without one the window keeps Qt's own
//   colours.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holla (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPalette>
#include <QString>
#include <QStringList>

namespace Win {

// Explorer's cut/copy marker on the clipboard, beside the file list: a
// DWORD drop effect, 2 (move) for a cut, 5 (copy and link) for a copy.
inline constexpr char kDropEffectMime[] = "application/x-qt-windows-mime;value=\"Preferred DropEffect\"";
QByteArray dropEffect(bool cut);
bool isCut(const QByteArray &dropEffect);

// Windows' own words for an error code (GetLastError, an HRESULT).
QString errorText(unsigned long error);

// This user's folder in a drive's Recycle Bin: "C:" -> "C:/$Recycle.Bin/S-1-5-...".
QString recycleBin(const QString &drive);

// The SYN theme's colours by key (SYN_BG, SYN_ACCENT, ...); empty when
// no theme can be found.
QHash<QString, QColor> themeColours();
// What to watch for a theme switch: .state\current-theme and the themes.
QStringList themeFiles();
// The palette qt6ct would make of `theme` (SYN-OS's qt6ct-colors template).
QPalette palette(const QHash<QString, QColor> &theme);
// foot.ini's keys (foreground, background, regular0..7, bright0..7) as
// SYN-OS's foot template fills them from `theme`.
QHash<QString, QColor> terminalColours(const QHash<QString, QColor> &theme);

// Lets the window's own alpha (the opacity setting) through to the
// desktop, which Qt leaves solid on a window with Windows' frame: the
// desktop as it is, no blur, as foot shows it on SYN-OS.
void seeThrough(quintptr winId);

// Terminus, as qt6ct sets it on SYN-OS (Terminess Nerd Font, the TrueType
// build SYN-WIN installs for its bar), when it's there.
bool hasTerminus();
QFont uiFont();
QFont fixedFont();

// A terminal in `dir`: Windows Terminal, else a console window of the
// shell's own; running `command` in the shell there if one is given.
bool openTerminal(const QString &dir, const QString &command, QString *error);

// Drives that are there and ready, as "C:/".
QStringList drives();
// "D:  USB STICK" for the menus.
QString driveLabel(const QString &root);

} // namespace Win
