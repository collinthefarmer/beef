#pragma once

#include "menu/Frame.h"

#include <string_view>

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] std::string_view DrawResourcesRule(const Frame &a_frame);
void DrawResources(const Frame &a_frame, std::string_view a_filter);
}
