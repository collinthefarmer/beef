// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
enum class RecipeId : std::size_t {};

struct CatalogEntry {
  Recipe recipe;
  std::uint64_t fingerprint = 0;
};

class RecipeCatalog {
public:
  [[nodiscard]] static RecipeCatalog Of(std::vector<CatalogEntry> a_entries);
  [[nodiscard]] std::span<const Recipe> Recipes() const noexcept;
  [[nodiscard]] const Recipe *At(RecipeId a_recipe) const noexcept;
  [[nodiscard]] std::uint64_t FingerprintOf(RecipeId a_recipe) const noexcept;

private:
  RecipeCatalog() = default;
  std::vector<Recipe> recipes_;
  std::vector<std::uint64_t> fingerprints_;
};

[[nodiscard]] std::uint64_t ContentFingerprint(const Recipe &a_recipe);
[[nodiscard]] std::shared_ptr<const Recipe>
SharedRecipe(const std::shared_ptr<const RecipeCatalog> &a_catalog,
             RecipeId a_recipe);
}
