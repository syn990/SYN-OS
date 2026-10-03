// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   PtySession: what lets a shell move between SYN-SHELL's terminal area
//   and foot without restarting. Each shell runs under a small holder
//   process (`syn-shell --pty-hold`, named syn-shell-pty) that owns its
//   pty and listens on a unix socket in $XDG_RUNTIME_DIR/syn-shell. A
//   client (the terminal area, or `syn-shell --attach` running inside
//   foot) connects and relays: the holder's output is the shell's output,
//   raw; the client sends framed packets (keystrokes, its size, a cd).
//
//   One client at a time. A new one takes over and the old one is
//   dropped: that's the teleport. When the client goes and nobody took
//   over, the shell is hung up, the same as closing a terminal window.
//
//   The shell is started with a zsh integration (bash gets the first half)
//   that reports every directory change as OSC 7, and on SIGUSR1 cds to
//   the folder the browser asked for and redraws its prompt. The holder
//   only sends that signal while the shell itself is in the foreground,
//   so a running vim or make is never interrupted.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace PtySession {

// Client -> holder packets: one type byte, a 4-byte little-endian length,
// the payload.
enum PacketType : char {
  Input = 'D', // bytes for the shell
  Size = 'S',  // "rows cols"
  Cd = 'C',    // a folder for the shell to cd to, if it's idle at a prompt
};

QByteArray packet(PacketType type, const QByteArray &payload);

// A fresh socket path under $XDG_RUNTIME_DIR/syn-shell (made 0700).
QString newSocketPath();

// The two command-line modes of the syn-shell binary. Both return the
// process exit status.
// With a `command`, that runs on the pty instead of the user's shell
// (bulk rename's editor), with no shell integration.
int runHolder(const QString &socketPath, const QString &cwd, int rows, int cols,
              const QStringList &command = {});
int runAttach(const QString &socketPath);

} // namespace PtySession
