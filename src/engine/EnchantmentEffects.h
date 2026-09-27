// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] float
EnchantmentValueFor(const RE::MagicItem *a_item,
                    const std::optional<RecipeKey> &a_effectKey,
                    EnchantmentField a_field);
}
