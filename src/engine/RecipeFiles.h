#pragma once

#include "recipe/Recipe.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::expected<LoadResult, Diagnostic>
ReadRecipeFile(const std::filesystem::path &a_path, std::string_view a_id);

[[nodiscard]] std::expected<Recipe, Diagnostic>
WriteRecipeFile(const std::filesystem::path &a_path, const Recipe &a_recipe,
                bool a_promoteImported);

[[nodiscard]] std::optional<Diagnostic>
RenameRecipeFile(const std::filesystem::path &a_from,
                 const std::filesystem::path &a_to, std::string_view a_id,
                 bool a_owned);
[[nodiscard]] std::optional<Diagnostic>
DeleteRecipeFile(const std::filesystem::path &a_path, std::string_view a_id,
                 bool a_owned);
}
