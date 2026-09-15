#pragma once

#include "menu/Frame.h"
#include "studio/Intent.h"

#include <string_view>

namespace BetterEnchantmentEffects::Menu {
void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab);
[[nodiscard]] std::string_view DrawResourcesRule(const Frame &a_frame);
void DrawResources(const Frame &a_frame, std::string_view a_filter);
}
