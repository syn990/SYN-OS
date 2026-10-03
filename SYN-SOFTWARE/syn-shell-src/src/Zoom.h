// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   Zoom: the text sizes Ctrl+wheel steps through, in pixels. They're the
//   sizes Terminus is drawn at (it's a bitmap font, so a size between two
//   of these would just round to one of them), so at rest text is always
//   pixel-sharp; the step between two is animated by whoever zooms.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#pragma once

#include <QtGlobal>

#include <array>
#include <cstdlib>

namespace Zoom {

inline constexpr std::array<int, 9> kLadder{12, 14, 16, 18, 20, 22, 24, 28, 32};
inline constexpr int kAnimationMs = 150;

// `steps` sizes up (or down, negative) from the ladder size nearest `px`.
inline int step(int px, int steps)
{
  int nearest = 0;
  for (int i = 1; i < int(kLadder.size()); ++i)
    if (std::abs(kLadder[i] - px) < std::abs(kLadder[nearest] - px))
      nearest = i;
  return kLadder[qBound(0, nearest + steps, int(kLadder.size()) - 1)];
}

} // namespace Zoom
