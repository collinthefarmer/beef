#pragma once

#include "recipe/Recipe.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct Selection {
  PieceRef piece;
  std::string recipeID;
  std::string geometry;
  Target target = Target::kMaterial;
  std::optional<Slot> slot;
  std::optional<std::size_t> layer;
};

[[nodiscard]] const PieceRow *
SelectedPiece(const Snapshot &a_snapshot,
              const Selection &a_selection) noexcept;
[[nodiscard]] const RecipeRow *
SelectedRecipe(const PieceRow *a_piece, const Selection &a_selection) noexcept;
[[nodiscard]] const GeometryRow *
SelectedGeometry(const RecipeRow *a_recipe,
                 const Selection &a_selection) noexcept;
[[nodiscard]] const OutputRow *
SelectedOutput(const GeometryRow *a_geometry,
               const Selection &a_selection) noexcept;
[[nodiscard]] std::optional<PieceRef>
RequestOf(const Selection &a_selection) noexcept;
void ResolveSelection(Selection &a_selection, const Snapshot &a_snapshot);
struct ViewedRecipesInput {
  std::vector<ResolvedRecipe> resolved;
  const WornPiece &piece;
  PieceRef ref;
  const View &view;
  std::span<const Recipe> loaded;
};

[[nodiscard]] std::vector<ResolvedRecipe>
ViewedRecipes(ViewedRecipesInput a_input);
}
