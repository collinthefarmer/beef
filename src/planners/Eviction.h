// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

namespace BetterEnchantmentEffects {
inline constexpr float kRestoreFraction = 0.8f;

enum class EvictionAction {
  kNone,
  kEvict,
  kRestore,
};

[[nodiscard]] constexpr EvictionAction
EvictionFor(float a_distance, float a_evictDistance, bool a_applied) noexcept {
  if (a_evictDistance <= 0.0f) {
    return EvictionAction::kNone;
  }
  if (a_applied) {
    return a_distance > a_evictDistance ? EvictionAction::kEvict
                                        : EvictionAction::kNone;
  }
  return a_distance < a_evictDistance * kRestoreFraction
             ? EvictionAction::kRestore
             : EvictionAction::kNone;
}

[[nodiscard]] constexpr bool Returns(float a_distance,
                                     float a_evictDistance) noexcept {
  return EvictionFor(a_distance, a_evictDistance, false) ==
             EvictionAction::kRestore ||
         a_evictDistance <= 0.0f;
}
}
