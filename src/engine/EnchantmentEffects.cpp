// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/EnchantmentEffects.h"

#include "engine/EngineForms.h"

namespace BetterEnchantmentEffects {
namespace {
bool MatchesEffect(const RE::Effect &a_effect, const RecipeKey &a_key) {
  const FormRef *form = a_key.Form();
  if (!form || !form->key || !a_effect.baseEffect) {
    return false;
  }
  if (a_key.kind == KeyKind::kMagicEffect) {
    return FormKeyFor(*a_effect.baseEffect) == *form->key;
  }
  if (a_key.kind == KeyKind::kEffectShader) {
    const auto *shader = ShaderFor(a_effect.baseEffect);
    return shader && FormKeyFor(*shader) == *form->key;
  }
  return false;
}

const RE::Effect *SelectedEffect(const RE::MagicItem &a_item,
                                 const std::optional<RecipeKey> &a_effectKey) {
  if (!a_effectKey) {
    return a_item.GetCostliestEffectItem();
  }
  const RE::Effect *selected = nullptr;
  for (const RE::Effect *effect : a_item.effects) {
    if (effect && MatchesEffect(*effect, *a_effectKey) &&
        (!selected || effect->cost > selected->cost)) {
      selected = effect;
    }
  }
  return selected;
}
}

float EnchantmentValueFor(const RE::MagicItem *a_item,
                          const std::optional<RecipeKey> &a_effectKey,
                          EnchantmentField a_field) {
  const RE::Effect *effect =
      a_item ? SelectedEffect(*a_item, a_effectKey) : nullptr;
  if (!effect) {
    return 0.0f;
  }
  switch (a_field) {
  case EnchantmentField::kMagnitude:
    return effect->GetMagnitude();
  case EnchantmentField::kCost:
    return effect->cost;
  }
  return 0.0f;
}
}
