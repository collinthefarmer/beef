#pragma once

#include "Edits.h"
#include "Recipe.h"
#include "Paint.h"
#include "Signals.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace WornEnchantmentPBR
{
	struct RecipeStoreStatus
	{
		std::size_t loaded = 0;
		std::size_t withErrors = 0;
		std::size_t unresolved = 0;
		std::size_t imported = 0;
	};

	[[nodiscard]] std::filesystem::path RecipeDirectory();

	RecipeStoreStatus LoadRecipes();

	[[nodiscard]] RecipeStoreStatus GetRecipeStoreStatus() noexcept;

	[[nodiscard]] std::span<const Recipe> LoadedRecipes() noexcept;
	[[nodiscard]] const Studio::RegionsFile& LoadedPresets() noexcept;

	struct RecipeOrigin
	{
		std::filesystem::path         path;
		std::span<const Diagnostic>   diagnostics;
	};
	[[nodiscard]] std::optional<RecipeOrigin> OriginOf(const Recipe& a_recipe) noexcept;

	[[nodiscard]] std::shared_ptr<const SignalGraph> GraphFor(const Recipe& a_recipe);
	[[nodiscard]] const Studio::ReferenceCounts* ReferencesOf(std::string_view a_id) noexcept;

	[[nodiscard]] Recipe* MutableRecipe(std::string_view a_id) noexcept;
	std::span<const Diagnostic> Revalidate(std::string_view a_id);
	[[nodiscard]] bool          IsDirty(std::string_view a_id) noexcept;
	[[nodiscard]] std::expected<std::filesystem::path, std::string> SaveRecipe(std::string_view a_id);
	[[nodiscard]] bool RevertRecipe(std::string_view a_id);
	[[nodiscard]] bool NewRecipe(std::string_view a_id, RecipeKey a_key, std::string_view a_geometry);
	[[nodiscard]] bool RenameRecipe(std::string_view a_from, std::string_view a_to);
	[[nodiscard]] bool AddTransientRecipe(Recipe a_recipe);
	[[nodiscard]] bool DropTransientRecipe(std::string_view a_id);
	[[nodiscard]] bool IsTransient(std::string_view a_id) noexcept;
}
