#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Snapshot.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
inline constexpr std::string_view kScratchMask = "scratch";
inline constexpr std::string_view kPaintRecipe = "paint";
inline constexpr int kPaintPriority = 1000;

struct PaintSession {
  std::string recipeID;
  Surface surface = Surface::kMaterial;
  std::set<std::string> readGeometries;
};

[[nodiscard]] std::optional<std::string> ScratchOf(const RecipeRow &a_recipe);

[[nodiscard]] SurfaceOutput PaintOutput(Surface a_surface);
[[nodiscard]] std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface);
[[nodiscard]] Recipe PaintRecipe(const Recipe &a_active, RecipeKey a_key,
                                 Surface a_surface);
[[nodiscard]] std::vector<RecipeEdit> KeepEdits(const Recipe &a_paint,
                                                const Recipe &a_active,
                                                std::string_view a_name);
}
