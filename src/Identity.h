#pragma once

// The plugin's name and everything on disk or in the scene that derives from
// it. The name comes from CMake's project(); a rename changes that line, the
// namespace and the menu title below, and nothing else spells it.

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#ifndef WEPBR_PLUGIN_NAME
#	define WEPBR_PLUGIN_NAME "WornEnchantmentPBR"
#endif

namespace WornEnchantmentPBR::Identity
{
	inline constexpr std::string_view kName = WEPBR_PLUGIN_NAME;
	inline constexpr std::string_view kMenuTitle = "Worn Enchantment PBR";
	// Prefix on scene nodes the plugin creates (shells, lights), so the apply
	// traversal can tell its own geometry from the armor's.
	inline constexpr std::string_view kNodePrefix = "WEPBR";
	// Folder under Data/Textures: the presenter textures (placeholder files a render target is shown through) and frame folders.
	inline constexpr std::string_view kTextureFolder = WEPBR_PLUGIN_NAME;

	inline std::string LogFileName()
	{
		return std::format("{}.log", kName);
	}

	inline std::filesystem::path IniPath()
	{
		return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / std::format("{}.ini", kName);
	}

	// Recipes: Data/<plugin>/<anything>/*.json, loaded recursively; the
	// importer writes under imported/, the menu saves under user/, which loads last.
	inline std::filesystem::path RecipeRoot()
	{
		return std::filesystem::path{ "Data" } / std::string{ kName };
	}

	inline std::filesystem::path ImportedRecipeFolder()
	{
		return RecipeRoot() / "imported";
	}

	inline std::filesystem::path UserRecipeFolder()
	{
		return RecipeRoot() / "user";
	}

	// The shipped region presets, beside the DLL rather than under the
	// recipe root, which loads every .json below it as a recipe.
	inline std::filesystem::path PresetsPath()
	{
		return std::filesystem::path{ "Data" } / "SKSE" / "Plugins" / std::string{ kName } / "regions.json";
	}

	inline std::string PresenterTexturePath(std::uint32_t a_index)
	{
		return std::format("textures\\{}\\slots\\slot_{:02}.dds", kTextureFolder, a_index);
	}

	inline std::string ShellNodeSuffix()
	{
		return std::format(" [{} shell]", kNodePrefix);
	}

	inline std::string LightNodeName()
	{
		return std::format("{} light", kNodePrefix);
	}
}
