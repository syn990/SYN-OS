// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   syn-shell-attach (Windows): `syn-shell --attach` on its own. The
//   window is a GUI program, which Windows Terminal can't host, so the
//   shell moves there through this console program instead: it takes the
//   running shell over from the terminal area (see PtySession_win.cpp).
//
//     syn-shell-attach PIPE
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "PtySession.h"

#include <cstdio>

int main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "usage: syn-shell-attach PIPE\n");
    return 2;
  }
  return PtySession::runAttach(QString::fromLocal8Bit(argv[1]));
}
