#pragma once

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#ifndef BEEF_PLUGIN_NAME
#define BEEF_PLUGIN_NAME "BetterEnchantmentEffects"
#endif

namespace BetterEnchantmentEffects::Identity {
inline constexpr std::string_view kName = BEEF_PLUGIN_NAME;
inline constexpr std::string_view kMenuTitle = "Better Enchantment Effects";
inline constexpr std::string_view kNodePrefix = "BEEF";
inline constexpr std::string_view kTextureFolder = BEEF_PLUGIN_NAME;

inline std::string LogFileName() { return std::format("{}.log", kName); }

inline std::string TraceFileName(std::string_view a_run) {
  return std::format("{}-trace-{}.jsonl", kName, a_run);
}

inline std::filesystem::path IniPath() {
  return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" /
         std::format("{}.ini", kName);
}

inline std::filesystem::path PluginFolder() {
  return std::filesystem::path{"Data"} / "SKSE" / "Plugins" /
         std::string{kName};
}

inline std::filesystem::path RecipeRoot() { return PluginFolder() / "recipes"; }

inline std::filesystem::path ImportedRecipeFolder() {
  return RecipeRoot() / "imported";
}

inline std::filesystem::path UserRecipeFolder() {
  return RecipeRoot() / "user";
}

inline std::filesystem::path PresetsPath() {
  return PluginFolder() / "presets.json";
}

inline std::filesystem::path TemplateFolder() {
  return PluginFolder() / "templates";
}

inline std::string PresenterTexturePath(std::uint32_t a_index) {
  return std::format("textures\\{}\\slots\\slot_{:02}.dds", kTextureFolder,
                     a_index);
}

inline std::string ShellNodeSuffix() {
  return std::format(" [{} shell]", kNodePrefix);
}

inline std::string LightNodeName() {
  return std::format("{} light", kNodePrefix);
}
}
