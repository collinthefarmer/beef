// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/PreviewWindows.h"

#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
struct LayerShift {
  std::size_t output = 0;
};
struct OutputShift {};
struct NoShift {};
using IndexShift = std::variant<NoShift, LayerShift, OutputShift>;

[[nodiscard]] IndexShift ShiftOf(const RecipeEdit &a_edit) {
  if (const auto *add = Get<AddLayer>(a_edit)) {
    return LayerShift{add->output};
  }
  if (const auto *remove = Get<RemoveLayer>(a_edit)) {
    return LayerShift{remove->output};
  }
  if (const auto *move = Get<MoveLayer>(a_edit)) {
    return LayerShift{move->output};
  }
  if (const auto *clear = Get<ClearLayers>(a_edit)) {
    return LayerShift{clear->output};
  }
  if (const auto *reset = Get<ResetOutput>(a_edit)) {
    return LayerShift{reset->output};
  }
  if (Is<AddOutput>(a_edit) || Is<RemoveOutput>(a_edit) ||
      Is<ClearOutputs>(a_edit) || Is<ClearRecipe>(a_edit) ||
      Is<AddLight>(a_edit)) {
    return OutputShift{};
  }
  return NoShift{};
}

[[nodiscard]] bool IsIndexed(const PreviewSubject &a_subject) noexcept {
  return Is<OutputSubject>(a_subject) || Is<LayerSubject>(a_subject);
}

[[nodiscard]] bool IsShiftedBy(const PreviewTarget &a_target,
                               const IndexShift &a_shift) {
  if (Is<OutputShift>(a_shift)) {
    return IsIndexed(a_target.subject);
  }
  const auto *layers = Get<LayerShift>(a_shift);
  const auto *layer = Get<LayerSubject>(a_target.subject);
  return layers && layer && layer->output == layers->output;
}

[[nodiscard]] const OutputRow *OutputOf(const GeometryRow &a_geometry,
                                        std::size_t a_index) {
  const OutputRow *output =
      FindBy(a_geometry.outputs, a_index, &OutputRow::index);
  return output && output->target != Target::kLight ? output : nullptr;
}

[[nodiscard]] PreviewPicture PictureOf(const PictureRow &a_row) {
  return {.texture = a_row.texture,
          .channel = a_row.channel,
          .animated = a_row.animated,
          .ids = a_row.ids,
          .problem = a_row.problem};
}

[[nodiscard]] PreviewPicture PictureOf(const OutputRow &a_row) {
  return {.texture = a_row.texture,
          .channel = ShaderChannel::kRgb,
          .animated = a_row.animated,
          .problem = a_row.problem};
}

[[nodiscard]] PreviewPicture PictureOf(const LayerRow &a_row, bool a_animated) {
  return {.texture = a_row.texture,
          .channel = ShaderChannel::kRgb,
          .animated = a_animated,
          .problem = a_row.problem};
}

[[nodiscard]] std::optional<PreviewPicture>
PictureIn(const GeometryRow &a_geometry, const PreviewSubject &a_subject) {
  return Match(
      a_subject,
      [&](const SourceSubject &a_source) -> std::optional<PreviewPicture> {
        const PictureRow *row = FindByName(a_geometry.sources, a_source.name);
        return row ? std::optional{PictureOf(*row)} : std::nullopt;
      },
      [&](const MaskSubject &a_mask) -> std::optional<PreviewPicture> {
        const PictureRow *row = FindByName(a_geometry.masks, a_mask.name);
        return row ? std::optional{PictureOf(*row)} : std::nullopt;
      },
      [&](const OutputSubject &a_output) -> std::optional<PreviewPicture> {
        const OutputRow *row = OutputOf(a_geometry, a_output.output);
        return row ? std::optional{PictureOf(*row)} : std::nullopt;
      },
      [&](const LayerSubject &a_layer) -> std::optional<PreviewPicture> {
        const OutputRow *row = OutputOf(a_geometry, a_layer.output);
        if (!row || a_layer.layer >= row->layers.size()) {
          return std::nullopt;
        }
        return PictureOf(row->layers[a_layer.layer], row->animated);
      });
}

[[nodiscard]] std::string OutputName(const GeometryRow *a_geometry,
                                     std::size_t a_index) {
  const OutputRow *row = a_geometry ? OutputOf(*a_geometry, a_index) : nullptr;
  if (!row) {
    return std::format("output {}", a_index + 1);
  }
  return std::format("{} / {}", SurfaceName(row->surface), SlotName(row->slot));
}

[[nodiscard]] std::string SubjectLabel(const GeometryRow *a_geometry,
                                       const PreviewSubject &a_subject) {
  return Match(
      a_subject,
      [](const SourceSubject &a_source) {
        return std::format("source {}", a_source.name);
      },
      [](const MaskSubject &a_mask) {
        return std::format("mask {}", a_mask.name);
      },
      [&](const OutputSubject &a_output) {
        return OutputName(a_geometry, a_output.output);
      },
      [&](const LayerSubject &a_layer) {
        return std::format("{} / layer {}",
                           OutputName(a_geometry, a_layer.output),
                           a_layer.layer + 1);
      });
}

template <class Predicate>
void DropWhere(PreviewWindows &a_windows, const Predicate &a_predicate) {
  std::erase_if(a_windows.open, [&](const PreviewWindow &a_window) {
    return a_predicate(a_window.target);
  });
}
}

std::uint32_t PixelsOf(PreviewTier a_tier) noexcept {
  switch (a_tier) {
  case PreviewTier::kSmall:
    return 128;
  case PreviewTier::kMedium:
    return 256;
  case PreviewTier::kLarge:
    return 512;
  }
  return 128;
}

std::string_view TierName(PreviewTier a_tier) noexcept {
  switch (a_tier) {
  case PreviewTier::kSmall:
    return "128 px";
  case PreviewTier::kMedium:
    return "256 px";
  case PreviewTier::kLarge:
    return "512 px";
  }
  return "128 px";
}

std::string_view PreviewChannelName(ShaderChannel a_channel) {
  switch (a_channel) {
  case ShaderChannel::kRgb:
    return "rgb";
  case ShaderChannel::kR:
    return "r";
  case ShaderChannel::kG:
    return "g";
  case ShaderChannel::kB:
    return "b";
  case ShaderChannel::kA:
    return "a";
  case ShaderChannel::kLuma:
    return "luma";
  }
  return "rgb";
}

std::optional<PreviewTarget> PreviewTargetFor(const Selection &a_selection,
                                              const RecipeRow *a_recipe,
                                              const GeometryRow *a_geometry,
                                              PreviewSubject a_subject) {
  if (!a_recipe || !a_geometry || a_selection.document) {
    return std::nullopt;
  }
  return PreviewTarget{a_selection.piece, a_recipe->id, a_geometry->name,
                       std::move(a_subject)};
}

bool IsSamePreview(const PreviewTarget &a_left,
                   const PreviewTarget &a_right) noexcept {
  return a_left.piece == a_right.piece && a_left.recipeID == a_right.recipeID &&
         a_left.subject == a_right.subject;
}

bool IsPinned(const PreviewWindows &a_windows,
              const PreviewTarget &a_target) noexcept {
  return std::ranges::any_of(a_windows.open, [&](const PreviewWindow &a_open) {
    return IsSamePreview(a_open.target, a_target);
  });
}

bool IsFull(const PreviewWindows &a_windows) noexcept {
  return a_windows.open.size() >= kMaxPreviewWindows;
}

PinOutcome TogglePin(PreviewWindows &a_windows, const PreviewTarget &a_target) {
  if (IsPinned(a_windows, a_target)) {
    DropWhere(a_windows, [&](const PreviewTarget &a_open) {
      return IsSamePreview(a_open, a_target);
    });
    return PinOutcome::kUnpinned;
  }
  if (IsFull(a_windows)) {
    return PinOutcome::kRefusedFull;
  }
  a_windows.open.push_back(
      PreviewWindow{.id = a_windows.nextID++, .target = a_target});
  return PinOutcome::kPinned;
}

void ClosePreview(PreviewWindows &a_windows, std::uint64_t a_id) {
  std::erase_if(a_windows.open, [&](const PreviewWindow &a_window) {
    return a_window.id == a_id;
  });
}

PreviewWindow *FindPreview(PreviewWindows &a_windows,
                           std::uint64_t a_id) noexcept {
  return FindById(a_windows.open, a_id);
}

const RecipeRow *RecipeOf(const Snapshot &a_snapshot,
                          const PreviewTarget &a_target) {
  const PieceRow *piece =
      FindBy(a_snapshot.pieces, a_target.piece, &PieceRow::ref);
  return piece ? FindById(piece->recipes, a_target.recipeID) : nullptr;
}

std::optional<PreviewContent> ResolvePreview(const Snapshot &a_snapshot,
                                             const PreviewTarget &a_target) {
  const RecipeRow *recipe = RecipeOf(a_snapshot, a_target);
  if (!recipe) {
    return std::nullopt;
  }
  if (recipe->geometries.empty()) {
    return PieceNotViewed{};
  }
  const GeometryRow *geometry =
      FindByName(recipe->geometries, a_target.geometry);
  if (!geometry) {
    return std::nullopt;
  }
  std::optional<PreviewPicture> picture =
      PictureIn(*geometry, a_target.subject);
  if (!picture) {
    return std::nullopt;
  }
  return std::move(*picture);
}

std::vector<std::string> PreviewGeometries(const Snapshot &a_snapshot,
                                           const PreviewTarget &a_target) {
  std::vector<std::string> names;
  const RecipeRow *recipe = RecipeOf(a_snapshot, a_target);
  if (!recipe) {
    return names;
  }
  for (const GeometryRow &geometry : recipe->geometries) {
    if (PictureIn(geometry, a_target.subject)) {
      names.push_back(geometry.name);
    }
  }
  return names;
}

std::string PreviewLabel(const Snapshot &a_snapshot,
                         const PreviewTarget &a_target) {
  if (a_target.recipeID == kPaintRecipe) {
    return "mask draft";
  }
  const RecipeRow *recipe = RecipeOf(a_snapshot, a_target);
  const GeometryRow *geometry =
      recipe ? FindByName(recipe->geometries, a_target.geometry) : nullptr;
  return std::format("{}: {}", a_target.recipeID,
                     SubjectLabel(geometry, a_target.subject));
}

void DropVanishedPreviews(PreviewWindows &a_windows,
                          const Snapshot &a_snapshot) {
  DropWhere(a_windows, [&](const PreviewTarget &a_target) {
    return !ResolvePreview(a_snapshot, a_target).has_value();
  });
}

void DropShiftedPreviews(PreviewWindows &a_windows, std::string_view a_recipeID,
                         std::span<const RecipeEdit> a_edits) {
  for (const RecipeEdit &edit : a_edits) {
    const IndexShift shift = ShiftOf(edit);
    DropWhere(a_windows, [&](const PreviewTarget &a_target) {
      return a_target.recipeID == a_recipeID && IsShiftedBy(a_target, shift);
    });
  }
}

void DropIndexedPreviews(PreviewWindows &a_windows,
                         std::string_view a_recipeID) {
  DropWhere(a_windows, [&](const PreviewTarget &a_target) {
    return a_target.recipeID == a_recipeID && IsIndexed(a_target.subject);
  });
}

void DropAllIndexedPreviews(PreviewWindows &a_windows) {
  DropWhere(a_windows, [](const PreviewTarget &a_target) {
    return IsIndexed(a_target.subject);
  });
}
}
