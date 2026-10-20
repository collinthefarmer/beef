// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeCatalog.h"
#include "recipe/Signals.h"
#include "studio/Edits.h"
#include "studio/Presets.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
struct RecipeStoreStatus {
  std::size_t loaded = 0;
  std::size_t withErrors = 0;
  std::size_t heldBack = 0;
  std::size_t unresolved = 0;
  std::size_t imported = 0;
  std::size_t debug = 0;
};

RecipeStoreStatus LoadRecipes();
[[nodiscard]] RecipeStoreStatus GetRecipeStoreStatus() noexcept;

[[nodiscard]] std::span<const Recipe> LoadedRecipes() noexcept;
[[nodiscard]] std::vector<Studio::UnappliedRecipeRow> UnappliedRecipes();
[[nodiscard]] std::shared_ptr<const RecipeCatalog>
AppliedCatalog(DebugRecipes a_debug) noexcept;
[[nodiscard]] const Studio::MaskPresets &LoadedPresets() noexcept;

struct RecipeOrigin {
  std::filesystem::path path;
  std::span<const Diagnostic> diagnostics;
};
[[nodiscard]] std::optional<RecipeOrigin>
OriginOf(const Recipe &a_recipe) noexcept;

[[nodiscard]] std::shared_ptr<const RecipeGraph>
GraphFor(const Recipe &a_recipe);
[[nodiscard]] std::shared_ptr<const RecipeGraph>
VariantGraphFor(const Recipe &a_recipe, const Variant &a_variant);
[[nodiscard]] const Studio::ReferenceCounts *
ReferencesOf(std::string_view a_id) noexcept;

[[nodiscard]] Recipe *MutableRecipe(std::string_view a_id) noexcept;
bool SetSignalHold(std::string_view a_id, std::string_view a_signal,
                   std::optional<Value> a_value);
[[nodiscard]] Studio::SignalHolds SignalHoldsOf(std::string_view a_id);
[[nodiscard]] std::vector<std::string> RecipesWithHolds();
std::span<const Diagnostic> RefreshRecipeDerivedState(std::string_view a_id);
[[nodiscard]] bool IsDirty(std::string_view a_id) noexcept;
[[nodiscard]] bool HasRecipeFile(std::string_view a_id) noexcept;
[[nodiscard]] std::expected<std::filesystem::path, Diagnostic>
SaveRecipe(std::string_view a_id);
[[nodiscard]] std::optional<Diagnostic> RevertRecipe(std::string_view a_id);
[[nodiscard]] std::optional<Diagnostic>
NewRecipe(std::string_view a_id, RecipeKey a_key, std::string_view a_geometry);
[[nodiscard]] std::optional<Diagnostic> RenameRecipe(std::string_view a_from,
                                                     std::string_view a_to);
[[nodiscard]] std::optional<Diagnostic> DeleteRecipe(std::string_view a_id);
[[nodiscard]] std::optional<Diagnostic> DuplicateRecipe(std::string_view a_from,
                                                        std::string_view a_to);
[[nodiscard]] std::optional<Diagnostic> AddTransientRecipe(Recipe a_recipe);
[[nodiscard]] std::optional<Diagnostic>
DropTransientRecipe(std::string_view a_id);
[[nodiscard]] bool IsTransient(std::string_view a_id) noexcept;
}
