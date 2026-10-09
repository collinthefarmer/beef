// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeCatalog.h"
#include "test_support.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe Named(std::string a_id, int a_priority) {
  Recipe recipe;
  recipe.id = std::move(a_id);
  recipe.priority = a_priority;
  return recipe;
}

void KeepsEachRecipeWithItsFingerprint() {
  const RecipeCatalog catalog =
      RecipeCatalog::Of({{Named("glow", 1), 11}, {Named("ring", 2), 22}});
  test::Equal(catalog.Recipes().size(), std::size_t{2},
              "the catalog holds every entry");
  const Recipe *ring = catalog.At(RecipeId{1});
  Check(ring && ring->id == "ring", "an id finds the recipe in entry order");
  test::Equal(catalog.FingerprintOf(RecipeId{1}), std::uint64_t{22},
              "an id finds the fingerprint of the same entry");
}

void AnswersNothingPastTheEnd() {
  const RecipeCatalog catalog = RecipeCatalog::Of({{Named("glow", 1), 11}});
  Check(catalog.At(RecipeId{1}) == nullptr,
        "an id past the end finds no recipe");
  test::Equal(catalog.FingerprintOf(RecipeId{7}), std::uint64_t{0},
              "an id past the end has fingerprint 0");
  Check(RecipeCatalog::Of({}).Recipes().empty(), "an empty catalog is valid");
}

void FingerprintFollowsContent() {
  const Recipe glow = Named("glow", 1);
  test::Equal(ContentFingerprint(glow), ContentFingerprint(Named("glow", 1)),
              "equal content gives an equal fingerprint");
  Check(ContentFingerprint(glow) != ContentFingerprint(Named("glow", 2)),
        "changed content gives a new fingerprint");
}

void ClassifiesOnlyTheTopLevelDebugFolder() {
  Check(ClassOfRecipePath("debug/x.json") == RecipeClass::kDebug,
        "a file in the top-level debug folder is a debug recipe");
  Check(ClassOfRecipePath("debug/deeper/x.json") == RecipeClass::kDebug,
        "a file nested under the top-level debug folder is a debug recipe");
  Check(ClassOfRecipePath("Debug/x.json") == RecipeClass::kDebug,
        "the debug folder name ignores case, as Windows does");
  Check(ClassOfRecipePath("examples/x.json") == RecipeClass::kRegular,
        "a file in another folder is a regular recipe");
  Check(ClassOfRecipePath("debug.json") == RecipeClass::kRegular,
        "a top-level file named debug.json is a regular recipe");
  Check(ClassOfRecipePath("examples/debug/x.json") == RecipeClass::kRegular,
        "a debug folder below another folder is not the debug folder");
  Check(ClassOfRecipePath("") == RecipeClass::kRegular,
        "an empty path is a regular recipe");
}

void HidesDebugEntriesOnlyWhenAsked() {
  const std::vector<CatalogEntry> entries{
      {Named("glow", 1), 11, RecipeClass::kRegular},
      {Named("tint", 2), 22, RecipeClass::kDebug},
      {Named("ring", 3), 33, RecipeClass::kRegular}};
  const RecipeCatalog hidden =
      RecipeCatalog::Of(MatchingEntries(entries, DebugRecipes::kHidden));
  test::Equal(hidden.Recipes().size(), std::size_t{2},
              "hidden debug recipes leave the matching catalog");
  const Recipe *second = hidden.At(RecipeId{1});
  Check(second && second->id == "ring" &&
            hidden.FingerprintOf(RecipeId{1}) == 33,
        "the regular recipes keep their order and fingerprints");
  const RecipeCatalog shown =
      RecipeCatalog::Of(MatchingEntries(entries, DebugRecipes::kShown));
  const Recipe *tint = shown.At(RecipeId{1});
  Check(shown.Recipes().size() == 3 && tint && tint->id == "tint",
        "shown debug recipes stay in the matching catalog in place");
}

void SharedRecipeKeepsItsCatalog() {
  auto catalog = std::make_shared<const RecipeCatalog>(
      RecipeCatalog::Of({{Named("glow", 4), 11}}));
  const std::shared_ptr<const Recipe> glow = SharedRecipe(catalog, RecipeId{0});
  catalog.reset();
  Check(glow && glow->priority == 4,
        "a shared recipe outlives the last other owner of its catalog");
  Check(!SharedRecipe(nullptr, RecipeId{0}), "no catalog gives no recipe");
  const auto empty =
      std::make_shared<const RecipeCatalog>(RecipeCatalog::Of({}));
  Check(!SharedRecipe(empty, RecipeId{0}),
        "an id past the end gives no recipe");
}
}

int main() {
  KeepsEachRecipeWithItsFingerprint();
  AnswersNothingPastTheEnd();
  FingerprintFollowsContent();
  SharedRecipeKeepsItsCatalog();
  ClassifiesOnlyTheTopLevelDebugFolder();
  HidesDebugEntriesOnlyWhenAsked();
  return test::Finish("recipe catalog");
}
