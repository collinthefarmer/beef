// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/RecipeSections.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
LoadedRecipeRow Row(std::string a_id, std::string a_path) {
  LoadedRecipeRow row;
  row.id = std::move(a_id);
  row.path = std::move(a_path);
  return row;
}
}

int main() {
  Check(FolderOf(R"(Data\SKSE\Plugins\X\recipes\user\glow.json)") == "user",
        "a recipe's section is the folder that holds its file");
  Check(FolderOf("recipes/imported/a.json") == "imported",
        "forward slashes separate folders too");
  Check(FolderOf("") == kUnsavedSection &&
            FolderOf("a.json") == kUnsavedSection,
        "a recipe with no folder is listed as unsaved");

  const std::vector<LoadedRecipeRow> rows{
      Row("b", R"(recipes\user\b.json)"),
      Row("x", R"(recipes\Imported\x.json)"),
      Row("a", R"(recipes\user\a.json)"),
      Row("y", R"(recipes\imported\y.json)"), Row("draft", "")};
  const std::vector<RecipeSection> sections = RecipeSectionsOf(rows);
  Check(sections.size() == 3 && sections[0].folder == "Imported" &&
            sections[1].folder == kUnsavedSection &&
            sections[2].folder == "user",
        "sections are ordered by folder name, ignoring case");
  Check(sections[0].ids == std::vector<std::string>{"x", "y"} &&
            sections[2].ids == std::vector<std::string>{"b", "a"},
        "a section keeps its recipes in load order");

  return test::Finish("studio_recipesections");
}
