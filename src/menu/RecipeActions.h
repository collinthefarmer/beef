#pragma once

#include "menu/Frame.h"

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] bool RecipeFilePending(const Frame &a_frame);
void DrawRecipeFileActions(const Frame &a_frame);
}
