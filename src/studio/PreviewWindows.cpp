// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/PreviewWindows.h"

#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool IsIndexed(const PreviewSubject &a_subject) noexcept {
  return Is<OutputSubject>(a_subject) || Is<LayerSubject>(a_subject);
}

[[nodiscard]] std::optional<PreviewSubject>
PreviewSubjectAfter(const PreviewSubject &a_subject,
                    const Renumbering &a_renumbering) {
  if (const auto *output = Get<OutputSubject>(a_subject)) {
    const std::optional<std::size_t> to =
        OutputAfter(a_renumbering, output->output);
    return to ? std::optional<PreviewSubject>{OutputSubject{*to}}
              : std::nullopt;
  }
  if (const auto *layer = Get<LayerSubject>(a_subject)) {
    const std::optional<LayerPlace> to =
        LayerAfter(a_renumbering, {layer->output, layer->layer});
    return to ? std::optional<PreviewSubject>{LayerSubject{to->output,
                                                           to->layer}}
              : std::nullopt;
  }
  return a_subject;
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

void RenumberPreviews(PreviewWindows &a_windows, std::string_view a_recipeID,
                      const Renumbering &a_renumbering) {
  std::vector<PreviewWindow> kept;
  kept.reserve(a_windows.open.size());
  for (PreviewWindow &window : a_windows.open) {
    if (window.target.recipeID != a_recipeID) {
      kept.push_back(std::move(window));
    } else if (std::optional<PreviewSubject> subject =
                   PreviewSubjectAfter(window.target.subject, a_renumbering)) {
      window.target.subject = std::move(*subject);
      kept.push_back(std::move(window));
    }
  }
  a_windows.open = std::move(kept);
}

void DropAllIndexedPreviews(PreviewWindows &a_windows) {
  DropWhere(a_windows, [](const PreviewTarget &a_target) {
    return IsIndexed(a_target.subject);
  });
}
}
