#include "studio/ResolveOutput.h"

#include <cstddef>
#include <optional>

namespace BetterEnchantmentEffects::Studio {
float ResolvedOutput::Scalar(ScalarField a_field) const noexcept {
  return scalars[static_cast<std::size_t>(a_field)];
}

bool ResolvedOutput::IsNamed(ScalarField a_field) const noexcept {
  return named[static_cast<std::size_t>(a_field)];
}

ResolvedOutput ResolveOutput(const SurfaceOutput &a_output,
                             const SignalState &a_signals) {
  ResolvedOutput resolved;
  const float colorFallback = ScalarFallback(ScalarField::kColor);
  resolved.color = Vec3{colorFallback, colorFallback, colorFallback};
  for (std::size_t i = 0; i < kScalarFieldCount; ++i) {
    resolved.scalars[i] = ScalarFallback(static_cast<ScalarField>(i));
  }
  for (const ScalarField field : ScalarsOf(a_output.slot)) {
    const std::size_t index = static_cast<std::size_t>(field);
    if (field == ScalarField::kColor) {
      if (a_output.scalars.color) {
        resolved.color = a_signals.Resolve(*a_output.scalars.color);
        resolved.named[index] = true;
      }
    } else if (const std::optional<Param> *param =
                   ScalarOf(a_output.scalars, field);
               param && *param) {
      resolved.scalars[index] = a_signals.Resolve(**param);
      resolved.named[index] = true;
    }
  }
  resolved.opacities.reserve(a_output.stack.size());
  for (const Layer &layer : a_output.stack) {
    resolved.opacities.push_back(a_signals.Resolve(layer.opacity));
  }
  return resolved;
}

ResolvedLight ResolveLight(const LightOutput &a_light,
                           const SignalState &a_signals) {
  ResolvedLight resolved;
  resolved.color = a_signals.Resolve(a_light.color);
  resolved.intensity = a_signals.Resolve(a_light.intensity);
  resolved.size = a_signals.Resolve(a_light.size);
  resolved.cutoff = a_signals.Resolve(a_light.cutoff);
  return resolved;
}
}
