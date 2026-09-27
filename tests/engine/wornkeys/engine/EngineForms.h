// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
inline FormKey FormKeyFor(const RE::TESForm &a_form) {
  return FormKey{"demo.esp", a_form.id};
}
inline RE::TESEffectShader *ShaderFor(const RE::EffectSetting *a_magic) {
  return a_magic ? a_magic->shader : nullptr;
}
}
