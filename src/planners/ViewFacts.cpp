// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ViewFacts.h"

namespace BetterEnchantmentEffects {
bool InViewOf(const ViewInputs &a_inputs) noexcept {
  if (a_inputs.role == ActorRole::kPlayer || !a_inputs.cullOutOfView) {
    return true;
  }
  return a_inputs.camera.value_or(false) || a_inputs.inspected;
}

std::optional<float>
ScreenFractionOf(std::optional<float> a_raw, bool a_setting,
                 std::optional<bool> a_regressionOverride) noexcept {
  return a_regressionOverride.value_or(a_setting) ? a_raw
                                                  : std::optional<float>{1.0f};
}
}
