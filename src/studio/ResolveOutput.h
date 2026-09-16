#pragma once

#include "recipe/Recipe.h"
#include "recipe/Signals.h"

#include <array>
#include <cstddef>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct ResolvedOutput {
  std::array<float, kScalarFieldCount> scalars{};
  std::array<bool, kScalarFieldCount> named{};
  Vec3 color{1.0f, 1.0f, 1.0f};
  std::vector<float> opacities;

  [[nodiscard]] float Scalar(ScalarField a_field) const noexcept;
  [[nodiscard]] bool IsNamed(ScalarField a_field) const noexcept;
};

[[nodiscard]] ResolvedOutput ResolveOutput(const SurfaceOutput &a_output,
                                           const SignalState &a_signals);

struct ResolvedLight {
  Vec3 color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float size = 1.4142f;
  float cutoff = 1.0f;
};

[[nodiscard]] ResolvedLight ResolveLight(const LightOutput &a_light,
                                         const SignalState &a_signals);
}
