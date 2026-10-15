// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorMatches.h"
#include "planners/FieldProgram.h"
#include "planners/ValueIdentity.h"

#include <compare>
#include <cstdint>
#include <string_view>

namespace BetterEnchantmentEffects {
enum class TextureFormat { kRgba8, kRgba32Float, kR8, kR32Float, kRgba16Float };
enum class MipPolicy { kGenerate, kNone };
inline constexpr std::size_t kMaxIdentityBytes = std::size_t{8} * 1024 * 1024;
struct SideRef {
  std::uint32_t index = 0;
  [[nodiscard]] bool operator==(const SideRef &) const = default;
  [[nodiscard]] std::strong_ordering
  operator<=>(const SideRef &) const = default;
};
struct TextureRequirements {
  SideRef side;
  TextureFormat format = TextureFormat::kRgba8;
  MipPolicy mipPolicy = MipPolicy::kGenerate;
  [[nodiscard]] bool operator==(const TextureRequirements &) const = default;
};
[[nodiscard]] constexpr bool IsUnorm8(TextureFormat a_format) noexcept {
  return a_format == TextureFormat::kRgba8 || a_format == TextureFormat::kR8;
}
[[nodiscard]] constexpr TextureFormat
FormatFor(bool a_scalar, TextureFormat a_precision) noexcept {
  if (IsUnorm8(a_precision)) {
    return a_scalar ? TextureFormat::kR8 : TextureFormat::kRgba8;
  }
  return a_scalar ? TextureFormat::kR32Float : TextureFormat::kRgba32Float;
}
[[nodiscard]] constexpr std::uint64_t
BytesPerTexel(TextureFormat a_format) noexcept {
  switch (a_format) {
  case TextureFormat::kR8:
    return 1;
  case TextureFormat::kRgba8:
  case TextureFormat::kR32Float:
    return 4;
  case TextureFormat::kRgba16Float:
    return 8;
  case TextureFormat::kRgba32Float:
    return 16;
  }
  return 16;
}
[[nodiscard]] std::string_view FormatName(TextureFormat a_format) noexcept;
struct TextureKey {
  ValueIdentity identity;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const TextureKey &) const = default;
  [[nodiscard]] std::strong_ordering operator<=>(const TextureKey &other) const;
};
enum class TextureUseInput { kSource, kMask, kFunctionArgument };
struct TextureUse {
  PlacementId placement{};
  std::size_t output = 0;
  std::size_t layer = 0;
  TextureUseInput input = TextureUseInput::kSource;
  [[nodiscard]] bool operator==(const TextureUse &) const = default;
};
struct TextureValue {
  const RecipeGraph *graph = nullptr;
  OutputRef output;
  std::size_t instance = 0;
  [[nodiscard]] bool operator==(const TextureValue &) const = default;
};
using TextureDemandId = std::size_t;
struct TextureDemand {
  TextureKey key;
  TextureValue value;
  std::vector<TextureDemandId> dependencies;
  std::vector<TextureUse> dependents;
  std::optional<FieldProgram> program;
  GeometryId geometry{};
};
struct TextureDemandRequest {
  TextureValue value;
  TextureRequirements requirements;
  TextureUse use;
  GeometryId geometry{};
};
[[nodiscard]] std::expected<TextureDemandId, std::string>
CollectTextureDemand(std::vector<TextureDemand> &demands,
                     const TextureDemandRequest &request,
                     const ValueBindings &bindings);
}
