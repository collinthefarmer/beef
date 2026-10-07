// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderExecution.h"
#include "test_support.h"

#include <cstdint>
#include <optional>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void TheClockCountsRenderedTime() {
  Check(AdvancedRenderClock(0, std::nullopt, 5000) == 0,
        "a first frame starts the clock where it is");
  Check(AdvancedRenderClock(1000, 2000, 2016) == 1016,
        "a frame advances the clock by the gap since the last frame");
  Check(AdvancedRenderClock(1000, 2000, 9000) == 1000 + kMaxRenderAdvanceMS,
        "a long gap while hidden advances the clock by at most the cap");
  Check(AdvancedRenderClock(1000, 2000, 1500) == 1000,
        "a clock read before the last frame does not move the clock");
}

void HiddenTimeDoesNotAgeAnOutput() {
  std::uint64_t clock = 0;
  std::optional<std::uint64_t> last;
  std::uint64_t now = 10000;
  for (int frame = 0; frame < 10; ++frame, now += 16) {
    clock = AdvancedRenderClock(clock, last, now);
    last = now;
  }
  const std::uint64_t usedAt = clock;
  now += 60000;
  clock = AdvancedRenderClock(clock, last, now);
  Check(clock - usedAt <= kMaxRenderAdvanceMS,
        "a minute out of view ages an output by at most one frame's cap");
  last = now;
  for (int frame = 0; frame < 40; ++frame) {
    now += 16;
    clock = AdvancedRenderClock(clock, last, now);
    last = now;
  }
  Check(clock - usedAt > 500,
        "rendered frames still age an unused output past the release time");
}
}

int main() {
  TheClockCountsRenderedTime();
  HiddenTimeDoesNotAgeAnOutput();
  return test::Finish("render clock");
}
