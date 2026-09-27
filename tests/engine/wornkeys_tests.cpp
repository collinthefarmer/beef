// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/EnchantmentEffects.h"
#include "engine/WornKeys.h"
#include "test_support.h"

#include <algorithm>
#include <array>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  RE::TESObjectARMO armor;
  armor.id = 1;
  RE::BGSKeyword keyword;
  keyword.id = 2;
  armor.keywords = {nullptr, &keyword};
  RE::MagicItem enchantment;
  enchantment.id = 3;
  RE::EffectSetting first;
  first.id = 4;
  RE::EffectSetting second;
  second.id = 5;
  RE::TESEffectShader shader;
  shader.id = 6;
  RE::TESEffectShader secondaryShader;
  secondaryShader.id = 7;
  first.shader = &shader;
  second.shader = &secondaryShader;
  RE::Effect a{&first, 100.0f, 1000.0f};
  RE::Effect b{&second, 5.0f, 25.0f};
  RE::Effect empty;
  enchantment.effects = {nullptr, &a, &empty, &b, &a};
  const auto keys = WornKeysOf(&armor, &enchantment);
  Check(
      keys.magicEffects ==
          std::vector<FormKey>{{"demo.esp", 4}, {"demo.esp", 5}},
      "every valid base effect is collected once, including secondary effects");
  Check(keys.armor == FormKey{"demo.esp", 1} &&
            keys.enchantment == FormKey{"demo.esp", 3} &&
            keys.effectShaders ==
                std::vector<FormKey>{{"demo.esp", 6}, {"demo.esp", 7}} &&
            keys.keywords == std::vector<FormKey>{{"demo.esp", 2}},
        "armor, enchantment, shader selection and keyword collection are "
        "preserved");
  Recipe recipe;
  recipe.keys = {
      {KeyKind::kMagicEffect, FormRef{"secondary", FormKey{"demo.esp", 5}}}};
  const std::array recipes{recipe};
  Check(Resolve(keys, recipes).size() == 1,
        "the real matching path selects an enchantment's secondary effect");
  const auto choices = KeyChoicesOf(keys);
  Check(std::ranges::count(choices, KeyKind::kMagicEffect, &PieceKey::kind) ==
            2,
        "the piece's key picker offers every distinct magic effect");
  const RecipeKey magicKey{KeyKind::kMagicEffect,
                           FormRef{"secondary", FormKey{"demo.esp", 5}}};
  const RecipeKey shaderKey{
      KeyKind::kEffectShader,
      FormRef{"secondary shader", FormKey{"demo.esp", 7}}};
  recipe.keys = {shaderKey};
  const std::array shaderRecipes{recipe};
  Check(Resolve(keys, shaderRecipes).size() == 1,
        "secondary effect shaders participate in recipe selection");
  Check(std::ranges::count(choices, KeyKind::kEffectShader, &PieceKey::kind) ==
            2,
        "the key picker offers all distinct effect shaders");
  for (const auto &key : {magicKey, shaderKey}) {
    Check(EnchantmentValueFor(&enchantment, key,
                              EnchantmentField::kMagnitude) == 25.0f &&
              EnchantmentValueFor(&enchantment, key, EnchantmentField::kCost) ==
                  5.0f,
          "effect keys scope both signals to their matching effect");
  }
  Check(EnchantmentValueFor(&enchantment, {}, EnchantmentField::kMagnitude) ==
            1000.0f,
        "selectors without an effect keep the generic costliest fallback");
  RE::Effect stronger{&second, 7.0f, 40.0f};
  RE::Effect tied{&second, 7.0f, 45.0f};
  enchantment.effects = {&a, &b, &stronger, &tied};
  for (const auto &key : {magicKey, shaderKey}) {
    Check(EnchantmentValueFor(&enchantment, key,
                              EnchantmentField::kMagnitude) == 40.0f,
          "duplicate matching effects select highest cost with stable first "
          "ties");
  }
  enchantment.effects = {&a};
  for (const auto &key : {magicKey, shaderKey}) {
    Check(EnchantmentValueFor(&enchantment, key,
                              EnchantmentField::kMagnitude) == 0.0f,
          "a missing selected effect never falls back to an unrelated effect");
  }
  Check(EnchantmentValueFor(nullptr, magicKey, EnchantmentField::kCost) ==
                0.0f &&
            EnchantmentValueFor(
                &enchantment,
                RecipeKey{KeyKind::kMagicEffect, FormRef{"missing", {}}},
                EnchantmentField::kCost) == 0.0f,
        "missing enchantments and unresolved effect keys return zero");
  Check(WornKeysOf(&armor, nullptr).magicEffects.empty() &&
            !WornKeysOf(&armor, nullptr).Enchanted(),
        "unenchanted armor carries no magic-effect keys");
  Check(!WornKeysOf(nullptr, nullptr).Enchanted(),
        "missing armor and enchantment produce an empty safe match input");
  enchantment.effects.clear();
  Check(WornKeysOf(nullptr, &enchantment).Enchanted() &&
            WornKeysOf(nullptr, &enchantment).magicEffects.empty(),
        "an enchantment without effects remains enchanted without inventing "
        "effects");
  return test::Finish("engine_wornkeys");
}
