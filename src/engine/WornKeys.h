// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] WornPiece WornKeysOf(RE::TESObjectARMO *a_armor,
                                   RE::MagicItem *a_magic);
}
