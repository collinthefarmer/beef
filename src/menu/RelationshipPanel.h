#pragma once
#include "menu/Frame.h"

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] bool HasRelationships(const Frame &a_frame);
void DrawRelationships(const Frame &a_frame);
}
