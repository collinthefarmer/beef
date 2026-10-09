// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeCatalog.h"

#include "Core.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace BetterEnchantmentEffects {
RecipeCatalog RecipeCatalog::Of(std::vector<CatalogEntry> a_entries) {
  RecipeCatalog catalog;
  catalog.recipes_.reserve(a_entries.size());
  catalog.fingerprints_.reserve(a_entries.size());
  for (CatalogEntry &entry : a_entries) {
    catalog.recipes_.push_back(std::move(entry.recipe));
    catalog.fingerprints_.push_back(entry.fingerprint);
  }
  return catalog;
}

std::span<const Recipe> RecipeCatalog::Recipes() const noexcept {
  return recipes_;
}

const Recipe *RecipeCatalog::At(RecipeId a_recipe) const noexcept {
  const std::size_t index = IndexOf(a_recipe);
  return index < recipes_.size() ? &recipes_[index] : nullptr;
}

std::uint64_t RecipeCatalog::FingerprintOf(RecipeId a_recipe) const noexcept {
  const std::size_t index = IndexOf(a_recipe);
  return index < fingerprints_.size() ? fingerprints_[index] : 0;
}

RecipeClass ClassOfRecipePath(const std::filesystem::path &a_relative) {
  const std::filesystem::path normal = a_relative.lexically_normal();
  const auto first = normal.begin();
  if (first == normal.end() || std::next(first) == normal.end()) {
    return RecipeClass::kRegular;
  }
  return Lower(first->string()) == "debug" ? RecipeClass::kDebug
                                           : RecipeClass::kRegular;
}

std::vector<CatalogEntry> MatchingEntries(std::vector<CatalogEntry> a_entries,
                                          DebugRecipes a_debug) {
  if (a_debug == DebugRecipes::kHidden) {
    std::erase_if(a_entries, [](const CatalogEntry &a_entry) {
      return a_entry.recipeClass == RecipeClass::kDebug;
    });
  }
  return a_entries;
}

std::uint64_t ContentFingerprint(const Recipe &a_recipe) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char c : SerializeRecipe(a_recipe)) {
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::shared_ptr<const Recipe>
SharedRecipe(const std::shared_ptr<const RecipeCatalog> &a_catalog,
             RecipeId a_recipe) {
  if (!a_catalog) {
    return nullptr;
  }
  const Recipe *recipe = a_catalog->At(a_recipe);
  if (!recipe) {
    return nullptr;
  }
  return std::shared_ptr<const Recipe>(a_catalog, recipe);
}
}
