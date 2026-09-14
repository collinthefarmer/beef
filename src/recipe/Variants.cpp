#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
bool VariantApplies(const Variant &a_variant, const FormKey &a_armor) noexcept {
  const auto *armor = Get<FormRef>(a_variant.key);
  return armor && armor->key && *armor->key == a_armor;
}

bool VariantApplies(const Variant &a_variant,
                    const GeometryIdentity &a_geometry) {
  const auto *selector = Get<Selector>(a_variant.key);
  return selector && Matches(*selector, a_geometry);
}

Recipe ApplyVariant(const Recipe &a_recipe, const Variant &a_variant) {
  Recipe out = a_recipe;
  for (auto &s : out.signals) {
    const auto it = a_variant.overrides.find(s.name);
    if (it == a_variant.overrides.end()) {
      continue;
    }
    s.kind = ConstantSignal{it->second};
    s.curve.reset();
  }
  return out;
}
}
