// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   TestDrive: options for driving the window from a script, built only
//   with -DSYN_SHELL_TEST=ON (never in the package):
//     --shot FILE.png          save a picture of the window and leave
//     --shot-at MS             ... that many ms after it shows (default 3000)
//     --view-keys KEYS         keys pressed in the space view 1.5 s in
//                              (space: 3D, tab: the graph, letters as they are)
//     --view-click X,Y         a click there in the view first (negative: from
//                              the right / bottom edge)
//     --view-drag DX,DY        then a drag with the left button held, from
//                              the view's middle (turns the 3D view, looks round walking)
//     --size WxH               the window this big
//     --view-script STEPS      then these, one after another, ';' between:
//                              click:X,Y  hold:KEY:MS  key:KEY  wait:MS  look:DX,DY
//                              report (walking / mouse held / what E would do /
//                              an overlay up, on stderr)  shot:FILE.png (a picture now)
//                              goto:PATH (a folder: walking in it; a file, walking: to it, facing it)
//                              KEY: a letter, space, shift, esc, enter, left, right
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QString>

class QWidget;

class TestDrive
{
public:
  // argv[i] is one of these: takes it (and its value), true.
  bool take(int argc, char **argv, int &i);
  // After the window is shown.
  void start(QWidget *window) const;

private:
  QString m_shot, m_keys, m_click, m_drag, m_size, m_script;
  int m_shotAt = 3000;
};
