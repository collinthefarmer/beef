// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects {
using Millis = std::uint64_t;

struct Hold {
  std::optional<Millis> since;

  [[nodiscard]] constexpr Hold Update(bool a_condition,
                                      Millis a_now) const noexcept {
    if (!a_condition) {
      return {};
    }
    if (since && *since <= a_now) {
      return *this;
    }
    return {a_now};
  }

  [[nodiscard]] constexpr bool Due(Millis a_now,
                                   Millis a_delay) const noexcept {
    return since && *since <= a_now && a_now - *since >= a_delay;
  }

  [[nodiscard]] bool operator==(const Hold &) const = default;
};
}
