// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "studio/Edits.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class PreviewTier : std::uint8_t { kSmall, kMedium, kLarge };

inline constexpr std::array kPreviewTiers{
    PreviewTier::kSmall, PreviewTier::kMedium, PreviewTier::kLarge};

inline constexpr std::array kPreviewChannels{
    ShaderChannel::kRgb, ShaderChannel::kR, ShaderChannel::kG,
    ShaderChannel::kB, ShaderChannel::kA};

inline constexpr std::size_t kMaxPreviewWindows = 8;

using PreviewSubject =
    std::variant<SourceSubject, MaskSubject, OutputSubject, LayerSubject>;

struct PreviewTarget {
  PieceRef piece;
  std::string recipeID;
  std::string geometry;
  PreviewSubject subject;
  [[nodiscard]] bool operator==(const PreviewTarget &) const = default;
};

struct ScreenPoint {
  float x = 0.0f;
  float y = 0.0f;
  [[nodiscard]] bool operator==(const ScreenPoint &) const = default;
};

struct PreviewWindow {
  std::uint64_t id = 0;
  PreviewTarget target;
  PreviewTier tier = PreviewTier::kMedium;
  std::optional<ShaderChannel> channel;
  std::optional<ScreenPoint> position;
};

struct PreviewWindows {
  std::vector<PreviewWindow> open;
  std::uint64_t nextID = 1;
};

enum class PinOutcome : std::uint8_t { kPinned, kUnpinned, kRefusedFull };

struct PreviewPicture {
  TextureHandle texture{};
  ShaderChannel channel = ShaderChannel::kRgb;
  bool animated = false;
  bool ids = false;
  std::string problem;
};

struct PieceNotViewed {};

using PreviewContent = std::variant<PreviewPicture, PieceNotViewed>;

[[nodiscard]] std::uint32_t PixelsOf(PreviewTier a_tier) noexcept;
[[nodiscard]] std::string_view TierName(PreviewTier a_tier) noexcept;
[[nodiscard]] std::string_view PreviewChannelName(ShaderChannel a_channel);

[[nodiscard]] std::optional<PreviewTarget>
PreviewTargetFor(const Selection &a_selection, const RecipeRow *a_recipe,
                 const GeometryRow *a_geometry, PreviewSubject a_subject);
[[nodiscard]] bool IsSamePreview(const PreviewTarget &a_left,
                                 const PreviewTarget &a_right) noexcept;
[[nodiscard]] bool IsPinned(const PreviewWindows &a_windows,
                            const PreviewTarget &a_target) noexcept;
[[nodiscard]] bool IsFull(const PreviewWindows &a_windows) noexcept;
PinOutcome TogglePin(PreviewWindows &a_windows, const PreviewTarget &a_target);
void ClosePreview(PreviewWindows &a_windows, std::uint64_t a_id);
[[nodiscard]] PreviewWindow *FindPreview(PreviewWindows &a_windows,
                                         std::uint64_t a_id) noexcept;

[[nodiscard]] const RecipeRow *RecipeOf(const Snapshot &a_snapshot,
                                        const PreviewTarget &a_target);
[[nodiscard]] std::optional<PreviewContent>
ResolvePreview(const Snapshot &a_snapshot, const PreviewTarget &a_target);
[[nodiscard]] std::vector<std::string>
PreviewGeometries(const Snapshot &a_snapshot, const PreviewTarget &a_target);
[[nodiscard]] std::string PreviewLabel(const Snapshot &a_snapshot,
                                       const PreviewTarget &a_target);
void DropVanishedPreviews(PreviewWindows &a_windows,
                          const Snapshot &a_snapshot);
void DropShiftedPreviews(PreviewWindows &a_windows, std::string_view a_recipeID,
                         std::span<const RecipeEdit> a_edits);
void DropIndexedPreviews(PreviewWindows &a_windows,
                         std::string_view a_recipeID);
void DropAllIndexedPreviews(PreviewWindows &a_windows);
}
