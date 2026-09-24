// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
inline void RebuildGameObjectCatalogs() {}
inline std::optional<FormKey> ResolveEditorId(std::string_view) {
  return std::nullopt;
}
}
