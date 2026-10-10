// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TickClock.h"
#include "test_support.h"

#include <cstdint>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
constexpr std::uint32_t kLastBeforeWrap = 0xFFFF'FFF0;

void TheFirstReadingStartsTheClock() {
  const TickClock clock = ClockAfter({}, 1234);
  Equal(clock.now, Millis{1234}, "the first reading is the clock's time");
  Check(clock.lastReading == 1234u, "the first reading is remembered");
}

void AReadingAdvancesByTheElapsedTime() {
  const TickClock clock = ClockAfter(ClockAfter({}, 1000), 1016);
  Equal(clock.now, Millis{1016}, "a later reading advances the clock");
  Equal(ClockAfter(clock, 1016).now, Millis{1016},
        "the same reading leaves the clock in place");
}

void TheClockRunsPastTheWrap() {
  const TickClock before = ClockAfter({}, kLastBeforeWrap);
  const TickClock after = ClockAfter(before, 0x10);
  Equal(after.now, Millis{kLastBeforeWrap} + 0x20,
        "a reading past the wrap advances by the wrapped difference");
  Check(after.now > before.now, "the clock never runs backwards at the wrap");
  Equal(static_cast<std::uint32_t>(after.now), std::uint32_t{0x10},
        "the low 32 bits follow the reading");
  const TickClock twice = ClockAfter(ClockAfter(after, 0x8000'0000), 0x10);
  Equal(twice.now, Millis{kLastBeforeWrap} + 0x20 + 0x1'0000'0000,
        "a second wrap adds another full cycle");
}
}

int main() {
  TheFirstReadingStartsTheClock();
  AReadingAdvancesByTheElapsedTime();
  TheClockRunsPastTheWrap();
  return test::Finish("planners_tickclock");
}
