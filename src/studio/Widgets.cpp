// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Widgets.h"

#include <algorithm>
#include <format>

namespace BetterEnchantmentEffects::Studio {
std::optional<std::size_t> PresetIndexOf(const PresetSliderSpec &a_spec,
                                         std::uint32_t a_value) noexcept {
  for (std::size_t i = 0; i < a_spec.presets.size(); ++i) {
    if (a_spec.presets[i].value == a_value) {
      return i;
    }
  }
  return std::nullopt;
}

std::string PresetValueText(const PresetSliderSpec &a_spec,
                            std::uint32_t a_value) {
  if (const std::optional<std::size_t> preset = PresetIndexOf(a_spec, a_value);
      preset && !a_spec.presets[*preset].label.empty()) {
    return std::string{a_spec.presets[*preset].label};
  }
  return a_spec.unit.empty() ? std::to_string(a_value)
                             : std::format("{} {}", a_value, a_spec.unit);
}

std::uint32_t SliderValueOf(const PresetSliderSpec &a_spec,
                            std::int64_t a_dragged) noexcept {
  const std::int64_t low = std::min(a_spec.min, a_spec.max);
  const std::int64_t high = std::max(a_spec.min, a_spec.max);
  const std::int64_t step = std::max<std::int64_t>(a_spec.step, 1);
  const std::int64_t clamped = std::clamp(a_dragged, low, high);
  const std::int64_t snapped = (clamped + step / 2) / step * step;
  return static_cast<std::uint32_t>(std::clamp(snapped, low, high));
}
}
