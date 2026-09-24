// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once
#include "menu/Frame.h"
#include "studio/Forms.h"

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] bool FieldHasExpressionShelf(const Studio::FormField &a_field);
[[nodiscard]] bool FieldHasNumberShelf(const Studio::FormField &a_field,
                                       std::size_t a_minCount = 1);
void DrawExpressionOpener(const Studio::FormField &a_field,
                          const Frame &a_frame);
}
