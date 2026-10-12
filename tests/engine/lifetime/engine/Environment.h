// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once
#include "planners/IdentityFacts.h"
#include "planners/ResolutionLevel.h"
namespace BetterEnchantmentEffects {
struct ActorEnvironment {
  void SetAppliedResolution(ResolutionLevel) noexcept {}
  [[nodiscard]] MatchedShaderFacts MatchedShader() const { return {}; }
};
}
