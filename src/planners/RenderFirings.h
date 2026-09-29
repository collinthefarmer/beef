// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <optional>
#include <vector>

namespace BetterEnchantmentEffects {
struct RenderFiring {
  std::optional<Vec3> origin;
  float startTime = 0;
  [[nodiscard]] bool operator==(const RenderFiring &) const = default;
};
struct RenderFirings {
  std::vector<RenderFiring> firings;
  [[nodiscard]] bool operator==(const RenderFirings &) const = default;
};
}
