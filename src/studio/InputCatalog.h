// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Words.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
struct InputMeasureInfo {
  Measure measure;
  std::string_view description;
};
inline constexpr std::array<InputMeasureInfo, std::size(kMeasures)>
    kInputMeasures{{
        {Measure::kCurrent, "Value now, including modifiers and damage."},
        {Measure::kBase,
         "Base value before permanent and temporary modifiers."},
        {Measure::kPermanent, "Base value plus permanent modifiers."},
        {Measure::kTemporaryModifier, "Temporary modifier alone."},
        {Measure::kDamage,
         "Negated damage modifier; positive values usually mean depletion."},
        {Measure::kMax,
         "Permanent value plus temporary modifier, before damage."},
    }};

struct ActorValueSample {
  std::string name;
  std::array<std::optional<float>, kInputMeasures.size()> samples{};
};

struct ActorValueHelp {
  std::string description;
  std::string units;
};

[[nodiscard]] ActorValueHelp ActorValueHelpOf(std::string_view a_name);
[[nodiscard]] std::optional<float> SampleAt(const ActorValueSample &a_sample,
                                            Measure a_measure);
}
