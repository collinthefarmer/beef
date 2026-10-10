// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/Hold.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects {
struct TickClock {
  Millis now = 0;
  std::optional<std::uint32_t> lastReading;
  [[nodiscard]] bool operator==(const TickClock &) const = default;
};

[[nodiscard]] constexpr TickClock ClockAfter(TickClock a_clock,
                                             std::uint32_t a_reading) noexcept {
  if (!a_clock.lastReading) {
    return {a_reading, a_reading};
  }
  const std::uint32_t elapsed = a_reading - *a_clock.lastReading;
  return {a_clock.now + elapsed, a_reading};
}
}
