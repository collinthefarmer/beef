// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/WornKeys.h"

#include "engine/EngineForms.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
namespace {
void AddFormKey(std::vector<FormKey> &a_keys, const RE::TESForm *a_form) {
  if (!a_form) {
    return;
  }
  const FormKey key = FormKeyFor(*a_form);
  if (!std::ranges::contains(a_keys, key)) {
    a_keys.push_back(key);
  }
}
}

WornPiece WornKeysOf(RE::TESObjectARMO *a_armor, RE::MagicItem *a_magic) {
  WornPiece keys;
  if (a_armor) {
    keys.armor = FormKeyFor(*a_armor);
    for (std::uint32_t i = 0; i < a_armor->GetNumKeywords(); ++i) {
      if (const auto keyword = a_armor->GetKeywordAt(i); keyword && *keyword) {
        keys.keywords.push_back(FormKeyFor(**keyword));
      }
    }
  }
  if (a_magic) {
    keys.enchantment = FormKeyFor(*a_magic);
    for (const RE::Effect *effect : a_magic->effects) {
      if (effect) {
        AddFormKey(keys.magicEffects, effect->baseEffect);
        AddFormKey(keys.effectShaders, ShaderFor(effect->baseEffect));
      }
    }
  }
  return keys;
}
}
