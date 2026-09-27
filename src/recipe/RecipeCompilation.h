// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Expression.h"
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects::detail {
using RecipeDefinition = std::variant<Signal, Source, Mask, Curve>;
enum class DeclarationCategory { kSignal, kSpatial, kFunction };

struct DeclarationExpression {
  Program program;
  std::vector<std::size_t> valueBindings;
  std::vector<std::size_t> functionBindings;
};

struct RecipeDeclaration {
  RecipeDefinition definition;
  std::string name;
  std::string displayName;
  ValueType valueType = ValueType::kScalar;
  DeclarationCategory category = DeclarationCategory::kSignal;
  std::vector<std::size_t> dependencies;
  std::optional<DeclarationExpression> expression;
  std::optional<std::size_t> resultTransform;
  bool mayChangeOverTime = false;
  bool isDisabled = false;
};

}
