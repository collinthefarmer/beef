// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <cstdint>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr auto kMaxFilterTaps =
    static_cast<std::int32_t>(kMaxFilterRadius);

struct FilterPass {
  float blurred = 0.0f;
  float lowest = 0.0f;
  float highest = 0.0f;
};

[[nodiscard]] std::int32_t FilterTaps(float a_radius) noexcept;
[[nodiscard]] float FilterWeight(std::int32_t a_offset,
                                 std::int32_t a_taps) noexcept;
[[nodiscard]] std::vector<float>
FilteredField(std::span<const float> a_values,
              std::span<const std::uint16_t> a_charts, std::uint32_t a_side,
              FilterOperation a_operation, float a_radius);
}
