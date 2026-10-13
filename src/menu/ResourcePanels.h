// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "studio/Intent.h"
#include "studio/Selection.h"

#include <functional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Menu {
void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab);
void DrawResourceRename(
    const Frame &a_frame, const std::string &a_name,
    const std::function<Studio::RecipeEdit(std::string)> &a_edit);
void DrawResourceClone(const Frame &a_frame,
                       const Studio::InspectorSubject &a_subject);
[[nodiscard]] NameLabelSpec
ResourceLabel(const Frame &a_frame, const char *a_key, std::string_view a_name);
void PostResourceRename(const Frame &a_frame,
                        const Studio::InspectorSubject &a_subject,
                        std::string a_to);
}
