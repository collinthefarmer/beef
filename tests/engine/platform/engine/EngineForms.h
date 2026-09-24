#pragma once

#include "PCH.h"
#include "recipe/Importer.h"

namespace BetterEnchantmentEffects {
inline FormKey FormKeyFor(const RE::TESForm &) { return {}; }
inline RE::TESEffectShader *ShaderFor(const RE::EnchantmentItem *) {
  return nullptr;
}
inline EffectShaderRecord RecordFrom(const RE::TESEffectShader &) { return {}; }
}
