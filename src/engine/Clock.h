// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::uint32_t NowMS();

[[nodiscard]] constexpr float InstanceSpeed(float a_animationSpeed,
                                            float a_viewSpeed,
                                            float a_clockSpeed) noexcept {
  return a_animationSpeed * a_viewSpeed * a_clockSpeed;
}
}
