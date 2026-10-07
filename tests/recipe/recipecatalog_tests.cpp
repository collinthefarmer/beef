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
  return test::Finish("recipe catalog");
}
