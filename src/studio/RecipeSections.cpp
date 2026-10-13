// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/RecipeSections.h"

#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects::Studio {
namespace {
bool IsSeparator(char a_char) noexcept {
  return a_char == '\\' || a_char == '/';
}

std::string Folded(std::string_view a_text) {
  std::string folded{a_text};
  std::ranges::transform(folded, folded.begin(), [](unsigned char a_char) {
    return static_cast<char>(std::tolower(a_char));
  });
  return folded;
}
}

std::string FolderOf(std::string_view a_path) {
  const std::size_t file = a_path.find_last_of("\\/");
  if (file == std::string_view::npos) {
    return std::string{kUnsavedSection};
  }
  std::string_view parent = a_path.substr(0, file);
  while (!parent.empty() && IsSeparator(parent.back())) {
    parent.remove_suffix(1);
  }
  const std::size_t start = parent.find_last_of("\\/");
  const std::string_view folder =
      start == std::string_view::npos ? parent : parent.substr(start + 1);
  return folder.empty() ? std::string{kUnsavedSection} : std::string{folder};
}

std::vector<RecipeSection>
RecipeSectionsOf(std::span<const LoadedRecipeRow> a_recipes) {
  std::vector<RecipeSection> sections;
  for (const LoadedRecipeRow &recipe : a_recipes) {
    const std::string folder = FolderOf(recipe.path);
    const auto found =
        std::ranges::find_if(sections, [&](const RecipeSection &a_section) {
          return Folded(a_section.folder) == Folded(folder);
        });
    if (found == sections.end()) {
      sections.push_back({folder, {recipe.id}});
    } else {
      found->ids.push_back(recipe.id);
    }
  }
  std::ranges::stable_sort(
      sections, [](const RecipeSection &a_left, const RecipeSection &a_right) {
        const bool leftUser = Folded(a_left.folder) == kUserSection;
        const bool rightUser = Folded(a_right.folder) == kUserSection;
        if (leftUser != rightUser) {
          return leftUser;
        }
        return Folded(a_left.folder) < Folded(a_right.folder);
      });
  return sections;
}
}
