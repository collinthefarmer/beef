#pragma once

#include "recipe/Recipe.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct LayerStackRow {
  std::size_t index = 0;
  LayerRow layer;
  bool muted = false;
  bool soloed = false;
  bool selected = false;
};

struct ForeignRow {
  std::string recipeID;
  int priority = 0;
  LayerRow layer;
};

struct LayerStack {
  std::size_t output = 0;
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<LayerStackRow> rows;
  std::vector<ForeignRow> below;
  std::vector<ForeignRow> above;
  TextureHandle composite = nullptr;
  bool animated = false;
  std::uint32_t size = 0;
  std::string problem;
  std::vector<ScalarRow> scalars;
  std::vector<Blend> blends;
  std::vector<std::string> masks;
  std::vector<std::string> scalarSignals;
  std::vector<std::string> colorSignals;
  bool isolated = false;
};

[[nodiscard]] std::optional<LayerStack>
BuildStackView(const PieceRow &a_piece, const RecipeRow &a_recipe,
               const GeometryRow &a_geometry, const Selection &a_selection,
               const View &a_view);

struct Inspector {
  std::size_t output = 0;
  std::size_t layer = 0;
  Slot slot = Slot::kEmissive;
  LayerRow row;
  std::optional<PictureRow> source;
  std::optional<PictureRow> mask;
  std::vector<SignalRow> signals;
  std::optional<TextRow> curve;
  std::vector<Blend> blends;
  std::vector<std::string> sources;
  std::vector<std::string> masks;
  std::vector<std::string> curves;
  std::vector<std::string> scalarSignals;
  std::vector<std::string> colorSignals;
};

[[nodiscard]] std::optional<Inspector>
BuildInspector(const RecipeRow &a_recipe, const GeometryRow &a_geometry,
               const Selection &a_selection);

struct SignalNames {
  std::vector<std::string> scalar;
  std::vector<std::string> color;
  std::vector<std::string> vec2;
  std::vector<std::string> triggers;
};
[[nodiscard]] SignalNames SignalNamesOf(const RecipeRow &a_recipe);

struct SignalList {
  std::vector<SignalRow> tunable;
  std::vector<SignalRow> developer;
};
[[nodiscard]] SignalList BuildSignalList(const RecipeRow &a_recipe,
                                         const Layout &a_layout);

[[nodiscard]] LightRow LightRowOf(const Recipe &a_recipe);
[[nodiscard]] ShellRow ShellRowOf(const Recipe &a_recipe);
[[nodiscard]] SourceRow SourceRowOf(const Source &a_source,
                                    std::size_t a_references);
[[nodiscard]] std::optional<SourceKind> SourceKindOf(const SourceRow &a_row);
}
