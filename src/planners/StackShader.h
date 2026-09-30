// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ProgramShader.h"

#include <variant>

namespace BetterEnchantmentEffects {
struct SourceTexture {
  bool meshSpace = false;
  [[nodiscard]] auto operator<=>(const SourceTexture &) const = default;
};
struct MaskTexture {
  [[nodiscard]] auto operator<=>(const MaskTexture &) const = default;
};
struct SegmentRead {
  std::uint32_t segment = 0;
  [[nodiscard]] auto operator<=>(const SegmentRead &) const = default;
};
using SourceRead = std::variant<std::monostate, SourceTexture, SegmentRead>;
using MaskRead = std::variant<std::monostate, MaskTexture, SegmentRead>;
struct LayerShape {
  SourceRead source;
  std::uint32_t channel = 0;
  std::uint32_t blend = 0;
  std::uint32_t channels = 15;
  MaskRead mask;
  std::uint32_t maskChannel = 0;
  [[nodiscard]] auto operator<=>(const LayerShape &) const = default;
};
struct StackShape {
  bool base = false;
  std::vector<LayerShape> layers;
  std::vector<ProgramInstruction> code;
  std::vector<InputTextureSlot> slots;
  std::vector<ProgramSegment> segments;
  [[nodiscard]] auto operator<=>(const StackShape &) const = default;
};
inline constexpr std::string_view kGeneratedStackEntry = "PSGeneratedStack";
inline constexpr std::size_t kMaxStackLayers = 8;
[[nodiscard]] std::vector<ProgramInstruction>
CodeWithoutNumbers(std::span<const ProgramInstruction> code);
[[nodiscard]] bool HasSource(const LayerShape &layer);
[[nodiscard]] bool HasMask(const LayerShape &layer);
[[nodiscard]] ProgramSegment SourceSegment(const StackShape &shape,
                                           const LayerShape &layer);
[[nodiscard]] ProgramSegment MaskSegment(const StackShape &shape,
                                         const LayerShape &layer);
[[nodiscard]] std::expected<void, std::string>
CheckStackShape(const StackShape &shape);
[[nodiscard]] std::expected<std::string, std::string>
GenerateStackShader(const StackShape &shape);
}
