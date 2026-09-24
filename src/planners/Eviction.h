// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

namespace BetterEnchantmentEffects {
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
  return a_distance < a_evictDistance * 0.8f ? EvictionAction::kRestore
                                             : EvictionAction::kNone;
}
}
