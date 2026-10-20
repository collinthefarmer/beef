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
  Check(sections.size() == 3 && sections[0].folder == "user" &&
            sections[1].folder == "Imported" &&
            sections[2].folder == kUnsavedSection,
        "user recipes come first, then folders by name, ignoring case");
  Check(sections[1].ids == std::vector<std::string>{"x", "y"} &&
            sections[0].ids == std::vector<std::string>{"b", "a"},
        "a section keeps its recipes in load order");

  UnappliedRecipeRow heldBack{
      .id = "frost",
      .path = "user/frost.json",
      .reason = UnappliedReason::kHeldBack,
      .diagnostics = {
          MakeDiagnostic(Severity::kWarning, "key 0", "unused"),
          MakeDiagnostic(Severity::kError, "signal glow",
                         "unknown signal kind"),
          MakeDiagnostic(Severity::kError, "output 1", "bad slot")}};
  Check(UnappliedSummary(heldBack) ==
            "held back: signal glow: unknown signal kind (+1 more)",
        "a held-back summary names its first error and counts the rest");
  const UnappliedRecipeRow unreadable{.id = "broken",
                                      .reason = UnappliedReason::kUnreadable};
  Check(UnappliedSummary(unreadable) == "unreadable",
        "a summary without errors names only the reason");

  std::vector<LoadedRecipeRow> unsaved;
  Check(!UnsavedRecipesText(unsaved), "no edited recipe needs no warning");
  for (const std::string id : {"a", "b", "c", "d", "e", "f", "g", "h"}) {
    LoadedRecipeRow row = Row(id, "");
    row.dirty = id != "b";
    unsaved.push_back(row);
  }
  Check(UnsavedRecipesText(unsaved) ==
            "'a', 'c', 'd', 'e', 'f', 'g' and 1 more",
        "the warning names the edited recipes and counts the rest");

  return test::Finish("studio_recipesections");
}
