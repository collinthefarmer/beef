#include "studio/Panels.h"

#include "recipe/Words.h"
#include "studio/FieldParsing.h"
#include "studio/Names.h"
#include "studio/Rows.h"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::vector<Blend> BlendsFor(Slot a_slot) {
  std::vector<Blend> blends;
  for (const auto &row : kBlends) {
    if (BlendAllowed(a_slot, row.value)) {
      blends.push_back(row.value);
    }
  }
  return blends;
}

void FillScalarAndColorSignals(const RecipeRow &a_recipe,
                               std::vector<std::string> &a_scalar,
                               std::vector<std::string> &a_color) {
  for (const auto &signal : a_recipe.signals) {
    if (signal.type == ValueType::kScalar) {
      a_scalar.push_back(signal.name);
    } else if (signal.type == ValueType::kVec3) {
      a_color.push_back(signal.name);
    }
  }
}

[[nodiscard]] bool MergesBefore(const PieceRow &a_piece,
                                const RecipeRow &a_other,
                                const RecipeRow &a_recipe) noexcept {
  const auto mine =
      std::ranges::find(a_piece.recipes, a_recipe.id, &RecipeRow::id);
  const auto theirs =
      std::ranges::find(a_piece.recipes, a_other.id, &RecipeRow::id);
  if (mine == a_piece.recipes.end() || theirs == a_piece.recipes.end()) {
    return a_other.priority <= a_recipe.priority;
  }
  return theirs < mine;
}

void FillForeignRows(LayerStack &a_stack, const PieceRow &a_piece,
                     const RecipeRow &a_recipe, const GeometryRow &a_geometry) {
  const bool known = FindById(a_piece.recipes, a_recipe.id) != nullptr;
  std::optional<std::size_t> mine;
  for (const auto &output : a_geometry.outputs) {
    if (known && output.merged &&
        WritesCell(output, a_stack.surface, a_stack.slot)) {
      mine = output.merge;
      break;
    }
  }
  struct Neighbour {
    std::size_t merge = 0;
    const RecipeRow *recipe = nullptr;
    const OutputRow *output = nullptr;
  };
  std::vector<Neighbour> neighbours;
  for (const auto &other : a_piece.recipes) {
    if (other.id == a_recipe.id) {
      continue;
    }
    const auto *geometry = FindByName(other.geometries, a_geometry.name);
    if (geometry == nullptr) {
      continue;
    }
    for (const auto &output : geometry->outputs) {
      if (!output.merged ||
          !WritesCell(output, a_stack.surface, a_stack.slot)) {
        continue;
      }
      neighbours.push_back(Neighbour{output.merge, &other, &output});
    }
  }
  std::ranges::stable_sort(neighbours, {}, &Neighbour::merge);
  for (const auto &neighbour : neighbours) {
    const bool below = mine
                           ? neighbour.merge < *mine
                           : MergesBefore(a_piece, *neighbour.recipe, a_recipe);
    auto &rows = below ? a_stack.below : a_stack.above;
    for (const auto &layer : neighbour.output->layers) {
      rows.push_back(
          ForeignRow{neighbour.recipe->id, neighbour.recipe->priority, layer});
    }
  }
}

void AddSignalNamed(std::vector<SignalRow> &a_signals,
                    const RecipeRow &a_recipe, std::string_view a_text) {
  if (!IsWholeReference(a_text)) {
    return;
  }
  const auto *signal = FindByName(a_recipe.signals, ReferenceName(a_text));
  if (signal != nullptr && FindByName(a_signals, signal->name) == nullptr) {
    a_signals.push_back(*signal);
  }
}

std::optional<LayerStack> BuildStack(const RecipeRow &a_recipe,
                                     const OutputRow *output,
                                     const Selection &a_selection,
                                     const View &a_view) {
  if (output == nullptr || output->target == Target::kLight) {
    return std::nullopt;
  }
  LayerStack stack;
  stack.output = output->index;
  stack.surface = output->surface;
  stack.slot = output->slot;
  stack.rows.reserve(output->layers.size());
  for (std::size_t i = 0; i < output->layers.size(); ++i) {
    LayerStackRow row;
    row.index = i;
    row.layer = output->layers[i];
    row.muted = a_view.LayerMuted(a_recipe.id, output->index, i);
    row.soloed = a_view.isolation.TargetsLayer(a_recipe.id, output->index, i);
    row.selected = a_selection.layer && *a_selection.layer == i;
    stack.rows.push_back(std::move(row));
  }
  stack.composite = output->texture;
  stack.animated = output->animated;
  stack.size = output->size;
  stack.problem = output->problem;
  stack.scalars = output->scalars;
  stack.blends = BlendsFor(output->slot);
  stack.masks = a_recipe.masks;
  FillScalarAndColorSignals(a_recipe, stack.scalarSignals, stack.colorSignals);
  stack.isolated = a_view.isolation.TargetsOutput(a_recipe.id, output->index);
  return stack;
}

std::optional<Inspector> InspectOutput(const RecipeRow &a_recipe,
                                       const GeometryRow *a_geometry,
                                       const OutputRow *output,
                                       const Selection &a_selection) {
  if (output == nullptr || output->target == Target::kLight ||
      !a_selection.layer || *a_selection.layer >= output->layers.size()) {
    return std::nullopt;
  }
  Inspector inspector;
  inspector.output = output->index;
  inspector.layer = *a_selection.layer;
  inspector.slot = output->slot;
  inspector.row = output->layers[inspector.layer];
  const LayerRow &row = inspector.row;

  if (a_geometry && IsWholeReference(row.source)) {
    const auto name = ReferenceName(row.source);
    const PictureRow *image = FindByName(a_geometry->sources, name);
    if (image == nullptr) {
      image = FindByName(a_geometry->masks, name);
    }
    if (image != nullptr) {
      inspector.source = *image;
    }
  }
  if (a_geometry && !row.mask.empty()) {
    if (const PictureRow *image =
            FindByName(a_geometry->masks, ReferenceName(row.mask))) {
      inspector.mask = *image;
    }
  }
  AddSignalNamed(inspector.signals, a_recipe, row.opacityText);
  AddSignalNamed(inspector.signals, a_recipe, row.color);
  if (IsWholeReference(row.curve)) {
    if (const TextRow *curve =
            FindByName(a_recipe.curves, ReferenceName(row.curve))) {
      inspector.curve = *curve;
    }
  }
  inspector.blends = BlendsFor(output->slot);
  for (const auto &source : a_recipe.sourceRows) {
    inspector.sources.push_back(source.name);
  }
  for (const auto &mask : a_recipe.maskRows) {
    inspector.masks.push_back(mask.name);
  }
  for (const auto &curve : a_recipe.curves) {
    inspector.curves.push_back(curve.name);
  }
  FillScalarAndColorSignals(a_recipe, inspector.scalarSignals,
                            inspector.colorSignals);
  return inspector;
}

}

std::optional<LayerStack> BuildStackView(const StackViewInput &a_input) {
  auto stack = BuildStack(a_input.recipe,
                          SelectedOutput(&a_input.geometry, a_input.selection),
                          a_input.selection, a_input.view);
  if (stack) {
    FillForeignRows(*stack, a_input.piece, a_input.recipe, a_input.geometry);
  }
  return stack;
}

std::optional<LayerStack> BuildStackView(const RecipeRow &a_recipe,
                                         const Selection &a_selection,
                                         const View &a_view) {
  return BuildStack(a_recipe, SelectedAuthoredOutput(a_recipe, a_selection),
                    a_selection, a_view);
}

std::optional<Inspector> BuildInspector(const RecipeRow &a_recipe,
                                        const GeometryRow &a_geometry,
                                        const Selection &a_selection) {
  return InspectOutput(a_recipe, &a_geometry,
                       SelectedOutput(&a_geometry, a_selection), a_selection);
}

std::optional<Inspector> BuildInspector(const RecipeRow &a_recipe,
                                        const Selection &a_selection) {
  return InspectOutput(a_recipe, nullptr,
                       SelectedAuthoredOutput(a_recipe, a_selection),
                       a_selection);
}

SignalNames SignalNamesOf(const RecipeRow &a_recipe) {
  SignalNames names;
  for (const auto &signal : a_recipe.signals) {
    if (signal.type == ValueType::kScalar) {
      names.scalar.push_back(signal.name);
    } else if (signal.type == ValueType::kVec3) {
      names.color.push_back(signal.name);
    } else if (signal.type == ValueType::kVec2) {
      names.vec2.push_back(signal.name);
    }
    if (signal.kind == SignalKindId::kTrigger) {
      names.triggers.push_back(signal.name);
    }
  }
  return names;
}

}
