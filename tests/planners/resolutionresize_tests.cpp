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
  Check(DrawOf(true, 3, 3) == ResizeDraw::kDrawn,
        "every stack drawn once is drawn");
  Check(DrawOf(true, 2, 3) == ResizeDraw::kDrawing,
        "a stack not yet drawn keeps the build drawing");
  Check(DrawOf(true, 0, 0) == ResizeDraw::kDrawn,
        "an instance with no stacks is drawn at once");
  Check(DrawOf(false, 0, 3) == ResizeDraw::kOutOfView,
        "an actor out of view is not waiting for a draw");
}

void SwapWaitsForTheDraw() {
  Check(!SwapDue(kBegan, ResizeDraw::kDrawing, 101),
        "a build still drawing does not swap");
  Check(SwapDue(kBegan, ResizeDraw::kDrawn, 101), "a drawn build swaps");
  Check(SwapDue(kBegan, ResizeDraw::kOutOfView, 100),
        "a build out of view swaps at once");
  Check(
      !SwapDue(kBegan, ResizeDraw::kDrawing, 100 + kResizeSwapAfterFrames - 1),
      "a build drawing inside the frame limit waits");
  Check(SwapDue(kBegan, ResizeDraw::kDrawing, 100 + kResizeSwapAfterFrames),
        "a build that never finishes drawing swaps at the frame limit");
  Check(!SwapDue(kBegan, ResizeDraw::kDrawing, 50),
        "a frame before the build began does not swap");
}

void OneBuildAtATime() {
  const std::vector<ResizeCandidate> candidates{
      {false, 0.9f}, {true, 0.2f}, {true, 0.6f}, {true, 0.4f}};
  Check(NextBuild(candidates, 0) == std::optional<std::size_t>{2},
        "the pending actor largest on screen builds first");
  Check(!NextBuild(candidates, kResizesInFlight),
        "no build starts while one is in flight");
  Check(!NextBuild(std::vector<ResizeCandidate>{{false, 1.0f}}, 0),
        "an actor without a pending level does not build");
  Check(!NextBuild({}, 0), "no actors start no build");
}
}

int main() {
  DrawCountsEveryStack();
  SwapWaitsForTheDraw();
  OneBuildAtATime();
  return test::Finish("planners_resolutionresize");
}
