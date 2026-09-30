// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "studio/Relationships.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct RecipeSubject {
  [[nodiscard]] bool operator==(const RecipeSubject &) const = default;
};
struct ShellSubject {
  [[nodiscard]] bool operator==(const ShellSubject &) const = default;
};
struct OutputSubject {
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const OutputSubject &) const = default;
};
struct LayerSubject {
  std::size_t output = 0;
  std::size_t layer = 0;
  [[nodiscard]] bool operator==(const LayerSubject &) const = default;
};
struct SignalSubject {
  std::string name;
  [[nodiscard]] bool operator==(const SignalSubject &) const = default;
};
struct SourceSubject {
  std::string name;
  [[nodiscard]] bool operator==(const SourceSubject &) const = default;
};
struct MaskSubject {
  std::string name;
  [[nodiscard]] bool operator==(const MaskSubject &) const = default;
};
struct CurveSubject {
  std::string name;
  [[nodiscard]] bool operator==(const CurveSubject &) const = default;
};
using InspectorSubject =
    std::variant<RecipeSubject, ShellSubject, OutputSubject, LayerSubject,
                 SignalSubject, SourceSubject, MaskSubject, CurveSubject>;

struct Selection {
  PieceRef piece;
  std::string recipeID;
  std::string geometry;
  Target target = Target::kMaterial;
  std::optional<Slot> slot;
  std::optional<std::size_t> layer;
  InspectorSubject subject = RecipeSubject{};
  bool document = false;
  std::optional<PropertyLocation> property;
  [[nodiscard]] bool operator==(const Selection &) const = default;
};

struct GeometryChoice {
  std::vector<std::string> names;
  std::optional<std::size_t> selected;
};

[[nodiscard]] const PieceRow *
SelectedPiece(const Snapshot &a_snapshot,
              const Selection &a_selection) noexcept;
[[nodiscard]] const RecipeRow *
SelectedRecipe(const PieceRow *a_piece, const Selection &a_selection) noexcept;
[[nodiscard]] const RecipeRow *
SelectedRecipe(const Snapshot &a_snapshot,
               const Selection &a_selection) noexcept;
[[nodiscard]] const GeometryRow *
SelectedGeometry(const RecipeRow *a_recipe,
                 const Selection &a_selection) noexcept;
[[nodiscard]] const OutputRow *
SelectedOutput(const GeometryRow *a_geometry,
               const Selection &a_selection) noexcept;
[[nodiscard]] const OutputRow *
SelectedAuthoredOutput(const RecipeRow &a_recipe,
                       const Selection &a_selection) noexcept;
[[nodiscard]] GeometryChoice GeometryChoiceOf(const RecipeRow &a_recipe,
                                              std::string_view a_selected);
[[nodiscard]] const PictureRow *
ResourcePictureOf(const GeometryRow &a_geometry,
                  const InspectorSubject &a_subject);
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
