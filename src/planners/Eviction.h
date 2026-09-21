#pragma once

namespace BetterEnchantmentEffects {
enum class EvictionAction {
  kNone,
  kEvict,
  kRestore,
};

// A hysteresis band keeps an actor from thrashing at the boundary: an applied
// actor is evicted past a_distance, and an evicted one is restored only once it
// closes back inside 80% of it. a_distance <= 0 disables eviction.
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
