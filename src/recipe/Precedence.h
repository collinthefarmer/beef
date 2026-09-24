#pragma once

#include <compare>
#include <cstddef>

namespace BetterEnchantmentEffects {
struct Precedence {
  int priority = 0;
  std::size_t loadOrder = 0;
  [[nodiscard]] std::strong_ordering
  operator<=>(const Precedence &) const = default;
};
}
