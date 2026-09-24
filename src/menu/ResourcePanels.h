#pragma once

#include "menu/Frame.h"
#include "studio/Intent.h"

#include <functional>
#include <string>

namespace BetterEnchantmentEffects::Menu {
void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab);
void DrawResourceRename(
    const Frame &a_frame, const std::string &a_name,
    const std::function<Studio::RecipeEdit(std::string)> &a_edit);
}
