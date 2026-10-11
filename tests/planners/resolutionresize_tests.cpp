// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResolutionResize.h"
#include "test_support.h"

#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
const ResizeProgress kBegan{ResolutionLevel::kQuarter, ResolutionLevel::kHalf,
                            100};

void DrawCountsEveryStack() {
  Check(DrawOf({.shown = true, .drawn = 3, .stacks = 3}) == ResizeDraw::kDrawn,
        "every stack drawn once is drawn");
  Check(DrawOf({.shown = true, .drawn = 2, .stacks = 3}) ==
            ResizeDraw::kDrawing,
        "a stack not yet drawn keeps the resize drawing");
  Check(DrawOf({.shown = true, .drawn = 0, .stacks = 0}) == ResizeDraw::kDrawn,
        "an instance with no stacks is drawn at once");
  Check(DrawOf({.shown = false, .drawn = 0, .stacks = 3}) ==
            ResizeDraw::kOutOfView,
        "an actor out of view is not waiting for a draw");
}

void SwapWaitsForTheDraw() {
  Check(!SwapDue(kBegan, ResizeDraw::kDrawing, 101),
        "a resize still drawing does not swap");
  Check(SwapDue(kBegan, ResizeDraw::kDrawn, 101), "a drawn resize swaps");
  Check(SwapDue(kBegan, ResizeDraw::kOutOfView, 100),
        "a resize out of view swaps at once");
  Check(
      !SwapDue(kBegan, ResizeDraw::kDrawing, 100 + kResizeSwapAfterFrames - 1),
      "a resize drawing inside the frame limit waits");
  Check(SwapDue(kBegan, ResizeDraw::kDrawing, 100 + kResizeSwapAfterFrames),
        "a resize that never finishes drawing swaps at the frame limit");
  Check(!SwapDue(kBegan, ResizeDraw::kDrawing, 50),
        "a frame before the resize began does not swap");
}

void OneResizeAtATimeLargestFirst() {
  const std::vector<ResizeCandidate> candidates{
      {1, false, true, 0.9f},  {2, true, true, 0.2f},
      {3, true, true, 0.6f},   {4, true, true, 0.4f},
      {5, true, false, 0.95f}, {6, true, true, std::nullopt}};
  Check(NextResize(candidates, 0) == std::optional<std::uint32_t>{3},
        "the pending actor largest on screen resizes first, before actors "
        "out of view or unmeasured");
  Check(NextResize(std::vector<ResizeCandidate>{{5, true, false, 0.95f},
                                                {6, true, true, std::nullopt}},
                   0) == std::optional<std::uint32_t>{6},
        "an actor in view resizes before one out of view");
  Check(!NextResize(candidates, kResizesInFlight),
        "no resize starts while one is in flight");
  Check(!NextResize(std::vector<ResizeCandidate>{{1, false, true, 1.0f}}, 0),
        "an actor without a pending level does not resize");
  Check(!NextResize({}, 0), "no actors start no resize");
}
}

int main() {
  DrawCountsEveryStack();
  SwapWaitsForTheDraw();
  OneResizeAtATimeLargestFirst();
  return test::Finish("planners_resolutionresize");
}
