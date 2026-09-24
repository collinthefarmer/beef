// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] bool RecipeFilePending(const Frame &a_frame);
void DrawRecipeRuleStatus(const Frame &a_frame);
void DrawRecipeSaveRevert(const Frame &a_frame);
void DrawMaskDraftHints(const Frame &a_frame);
void DrawRecipeFileActions(const Frame &a_frame);
}
