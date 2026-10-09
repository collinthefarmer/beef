// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionResize.h"

namespace BetterEnchantmentEffects {
ResizeDraw DrawOf(const ResizeDrawState &a_state) noexcept {
  if (!a_state.inView) {
    return ResizeDraw::kOutOfView;
  }
  return a_state.drawn >= a_state.stacks ? ResizeDraw::kDrawn
                                         : ResizeDraw::kDrawing;
}

bool SwapDue(const ResizeProgress &a_progress, ResizeDraw a_draw,
             std::uint64_t a_frame) noexcept {
  if (a_draw != ResizeDraw::kDrawing) {
    return true;
  }
  return a_frame >= a_progress.beganFrame &&
         a_frame - a_progress.beganFrame >= kResizeSwapAfterFrames;
}

namespace {
bool ResizesBefore(const ResizeCandidate &a_left,
                   const ResizeCandidate &a_right) noexcept {
  if (a_left.inView != a_right.inView) {
    return a_left.inView;
  }
  return LargerOnScreen(a_left.screenFraction, a_right.screenFraction);
}
}

std::optional<std::uint32_t>
NextResize(std::span<const ResizeCandidate> a_candidates,
           std::size_t a_inFlight) noexcept {
  if (a_inFlight >= kResizesInFlight) {
    return std::nullopt;
  }
  std::optional<std::size_t> chosen;
  for (std::size_t i = 0; i < a_candidates.size(); ++i) {
    if (a_candidates[i].pending &&
        (!chosen || ResizesBefore(a_candidates[i], a_candidates[*chosen]))) {
      chosen = i;
    }
  }
  if (!chosen) {
    return std::nullopt;
  }
  return a_candidates[*chosen].actor;
}
}
