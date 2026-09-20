#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "studio/Edits.h"
#include "studio/Presets.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
struct RecipeStoreStatus {
  std::size_t loaded = 0;
  std::size_t withErrors = 0;
  std::size_t heldBack = 0;
  std::size_t unresolved = 0;
  std::size_t imported = 0;
};

RecipeStoreStatus LoadRecipes();
[[nodiscard]] RecipeStoreStatus GetRecipeStoreStatus() noexcept;

[[nodiscard]] std::span<const Recipe> LoadedRecipes() noexcept;
[[nodiscard]] const Studio::MaskPresets &LoadedPresets() noexcept;

struct RecipeOrigin {
  std::filesystem::path path;
  std::span<const Diagnostic> diagnostics;
};
[[nodiscard]] std::optional<RecipeOrigin>
OriginOf(const Recipe &a_recipe) noexcept;

[[nodiscard]] std::shared_ptr<const SignalGraph>
GraphFor(const Recipe &a_recipe);
[[nodiscard]] const Studio::ReferenceCounts *
ReferencesOf(std::string_view a_id) noexcept;

[[nodiscard]] Recipe *MutableRecipe(std::string_view a_id) noexcept;
std::span<const Diagnostic> RefreshRecipeDerivedState(std::string_view a_id);
[[nodiscard]] bool IsDirty(std::string_view a_id) noexcept;
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
