#pragma once

// Recipe files on disk: Data/<plugin>/<Mod>/*.json at any depth, loaded in
// path order, then Data/<plugin>/user/, which loads last. Loaded at
// kDataLoaded and on a menu reload; the manager reads the loaded list and
// the compiled signal graphs from here.

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
		std::size_t loaded = 0;      // files read
		std::size_t withErrors = 0;  // files whose rows carry at least one error
		std::size_t unresolved = 0;  // editor IDs no loaded form answered to
		std::size_t imported = 0;    // recipes written by the importer this load
	};

	[[nodiscard]] std::filesystem::path RecipeDirectory();

	// Reads every recipe file, resolves editor IDs against the loaded forms,
	// then writes an imported recipe under imported/ for each armor
	// enchantment's effect shader that no loaded recipe is keyed to, and
	// checks that each written file reads back identical. Game thread.
	RecipeStoreStatus LoadRecipes();

	[[nodiscard]] RecipeStoreStatus GetRecipeStoreStatus() noexcept;

	// The loaded recipes in load order (the order Resolve expects). Stable
	// until the next LoadRecipes.
	[[nodiscard]] std::span<const Recipe> LoadedRecipes() noexcept;
	// The shipped region presets, read with the recipes; empty when the file
	// is missing or malformed (logged).
	[[nodiscard]] const Studio::RegionsFile& LoadedPresets() noexcept;

	// Where a recipe came from and what its rows reported.
	struct RecipeOrigin
	{
		std::filesystem::path         path;
		std::span<const Diagnostic>   diagnostics;
	};
	[[nodiscard]] std::optional<RecipeOrigin> OriginOf(const Recipe& a_recipe) noexcept;

	// The compiled signal graph of a loaded recipe, compiled on first use.
	[[nodiscard]] std::shared_ptr<const SignalGraph> GraphFor(const Recipe& a_recipe);
	// How many places name each signal, curve and image of a loaded recipe,
	// counted when the recipe is loaded or changed, so the snapshot copies
	// counts rather than re-parsing every expression. Null when not loaded.
	[[nodiscard]] const Studio::ReferenceCounts* ReferencesOf(std::string_view a_id) noexcept;

	// Editing, game thread only, through the manager so that nothing wearing
	// the recipe holds pointers into it while it changes. The store's copy
	// changes in place; the file changes on SaveRecipe alone.
	[[nodiscard]] Recipe* MutableRecipe(std::string_view a_id) noexcept;
	// After an edit: re-resolves and re-validates the rows, republishes the
	// copy LoadedRecipes() shows, drops the compiled graph, marks it dirty.
	std::span<const Diagnostic> Revalidate(std::string_view a_id);
	[[nodiscard]] bool          IsDirty(std::string_view a_id) noexcept;
	// Writes the recipe to its file. An imported recipe is written to
	// user/<id>.json with its imported line dropped, and the store follows
	// it there; the importer's file stays as the record of what it wrote.
	[[nodiscard]] std::expected<std::filesystem::path, std::string> SaveRecipe(std::string_view a_id);
	// Reads the file back, discarding the edits.
	[[nodiscard]] bool RevertRecipe(std::string_view a_id);
	// An empty recipe under the id (a file stem: letters, digits, '-', '_'
	// and '.'), keyed as given, listed as user/<id>.json and dirty until
	// saved. The loaded list grows, which moves every recipe in it, so the
	// caller retires everything that points into it first. False when the
	// id is taken or is not a file stem.
	// A new recipe under the id, or the id with -2, -3 ... when a recipe has
	// it, keyed as given; with a geometry named, its first output is an empty
	// emissive stack on the material selecting that geometry alone.
	[[nodiscard]] bool NewRecipe(std::string_view a_id, RecipeKey a_key, std::string_view a_geometry);
	// A loaded recipe under a new id; its file moves with it when the file
	// is the user's, and a shipped or imported file stays (it loads again
	// under its old id at the next start; the log says so). Refused for a
	// taken id or the paint recipe.
	[[nodiscard]] bool RenameRecipe(std::string_view a_from, std::string_view a_to);
	// A transient recipe: in the loaded list like any other (so it resolves
	// and applies), never written, not listed as a file, gone on reload.
	// The list moves on add and on drop, so the caller retires everything
	// that points into it first. False when the id is taken.
	[[nodiscard]] bool AddTransientRecipe(Recipe a_recipe);
	[[nodiscard]] bool DropTransientRecipe(std::string_view a_id);
	[[nodiscard]] bool IsTransient(std::string_view a_id) noexcept;
}
