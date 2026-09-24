#pragma once

#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
inline void RebuildGameObjectCatalogs() {}
inline std::optional<FormKey> ResolveEditorId(std::string_view) {
  return std::nullopt;
}
}
