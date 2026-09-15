#pragma once
#include "menu/Frame.h"
#include "studio/Forms.h"

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] bool FieldHasExpressionShelf(const Studio::FormField &a_field);
void DrawExpressionOpener(const Studio::FormField &a_field,
                          const Frame &a_frame);
}
