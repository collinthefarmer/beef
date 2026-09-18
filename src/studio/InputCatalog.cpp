#include "studio/InputCatalog.h"

#include "studio/Names.h"

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

ActorValueHelp ActorValueHelpOf(std::string_view a_name) {
  ActorValueHelp help;
  help.description = "Actor value read from the wearer. Bounds depend on the "
                     "value and installed mods.";
  for (const ResourceInfo &resource : kResources) {
    if (a_name.size() == resource.name.size() &&
        NameMatches(a_name, resource.name)) {
      help.description = std::string{resource.description};
      help.units = "points";
      break;
    }
  }
  return help;
}

std::optional<float> SampleAt(const ActorValueSample &a_sample,
                              Measure a_measure) {
  for (std::size_t i = 0; i < kInputMeasures.size(); ++i) {
    if (kInputMeasures[i].measure == a_measure) {
      return a_sample.samples[i];
    }
  }
  return std::nullopt;
}
}
