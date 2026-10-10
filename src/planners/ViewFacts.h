// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorRole.h"

#include <optional>

namespace BetterEnchantmentEffects {
struct ViewInputs {
  ActorRole role = ActorRole::kOther;
  bool cullOutOfView = true;
  bool inspected = false;
  std::optional<bool> camera;
};

[[nodiscard]] bool InViewOf(const ViewInputs &a_inputs) noexcept;
[[nodiscard]] std::optional<float>
ScreenFractionOf(std::optional<float> a_raw, bool a_setting,
                 std::optional<bool> a_regressionOverride) noexcept;
}
