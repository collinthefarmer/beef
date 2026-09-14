#pragma once

#include "recipe/Words.h"

#include <array>
#include <optional>
#include <span>
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

struct ActorInputInfo {
  std::string name;
  std::string label;
  std::string description;
  std::string units;
  std::array<std::optional<float>, kInputMeasures.size()> samples{};
};

[[nodiscard]] ActorInputInfo DescribeActorInput(std::string a_name,
                                                std::string a_label);
[[nodiscard]] std::optional<float> InputSample(const ActorInputInfo &a_input,
                                               Measure a_measure);
[[nodiscard]] bool InputMatches(const ActorInputInfo &a_input,
                                std::string_view a_filter);
}
