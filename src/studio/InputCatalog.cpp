#include "studio/InputCatalog.h"

#include "studio/Names.h"

#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
struct ResourceInfo {
  std::string_view name;
  std::string_view description;
};
inline constexpr ResourceInfo kResources[]{
    {"Health", "The wearer's health; zero or less is depleted."},
    {"Magicka", "The wearer's magicka used to cast spells."},
    {"Stamina", "The wearer's stamina used for sprinting and power attacks."},
};
}

ActorInputInfo DescribeActorInput(std::string a_name, std::string a_label) {
  ActorInputInfo result;
  result.name = std::move(a_name);
  result.label = a_label.empty() ? result.name : std::move(a_label);
  result.description = "Actor value read from the wearer. Bounds depend on the "
                       "value and installed mods.";
  for (const ResourceInfo &resource : kResources) {
    if (result.name.size() == resource.name.size() &&
        NameMatches(result.name, resource.name)) {
      result.description = resource.description;
      result.units = "points";
      break;
    }
  }
  return result;
}

std::optional<float> InputSample(const ActorInputInfo &a_input,
                                 Measure a_measure) {
  for (std::size_t i = 0; i < kInputMeasures.size(); ++i) {
    if (kInputMeasures[i].measure == a_measure) {
      return a_input.samples[i];
    }
  }
  return std::nullopt;
}

bool InputMatches(const ActorInputInfo &a_input, std::string_view a_filter) {
  return NameMatches(a_input.name, a_filter) ||
         NameMatches(a_input.label, a_filter) ||
         NameMatches(a_input.description, a_filter);
}
}
