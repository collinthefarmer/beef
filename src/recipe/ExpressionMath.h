// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

namespace BetterEnchantmentEffects {
inline constexpr float kMaxShuffleTotal = 251.0f;
inline constexpr float kShuffleOutside = 256.0f;

[[nodiscard]] float ShufflePosition(float a_index, float a_total,
                                    float a_seed) noexcept;
[[nodiscard]] float HashUnit(float a_value, float a_salt) noexcept;
[[nodiscard]] float RandomInRange(float a_start, float a_stop,
                                  float a_seed) noexcept;
[[nodiscard]] Vec3 HueColor(float a_turns) noexcept;
[[nodiscard]] Vec3 ShiftHue(const Vec3 &a_color, float a_turns) noexcept;
}
