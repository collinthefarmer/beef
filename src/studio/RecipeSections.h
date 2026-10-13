// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Snapshot.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
inline constexpr std::string_view kUnsavedSection = "unsaved";
inline constexpr std::string_view kUserSection = "user";

struct RecipeSection {
  std::string folder;
  std::vector<std::string> ids;
};

[[nodiscard]] std::string FolderOf(std::string_view a_path);
[[nodiscard]] std::vector<RecipeSection>
RecipeSectionsOf(std::span<const LoadedRecipeRow> a_recipes);
}
