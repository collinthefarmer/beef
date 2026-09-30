// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ProgramShader.h"

#include <variant>

namespace BetterEnchantmentEffects {
struct TextureSourceRead {
  bool meshSpace = false;
  [[nodiscard]] auto operator<=>(const TextureSourceRead &) const = default;
};
struct TextureMaskRead {
  [[nodiscard]] auto operator<=>(const TextureMaskRead &) const = default;
};
struct FieldRead {
  std::uint32_t segment = 0;
  [[nodiscard]] auto operator<=>(const FieldRead &) const = default;
};
using SourceRead = std::variant<std::monostate, TextureSourceRead, FieldRead>;
using MaskRead = std::variant<std::monostate, TextureMaskRead, FieldRead>;
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
  std::vector<InterpreterInstruction> code;
  std::vector<TextureSlot> slots;
  std::vector<InterpreterSegment> segments;
  [[nodiscard]] auto operator<=>(const StackShape &) const = default;
};
inline constexpr std::string_view kGeneratedStackEntry = "PSGeneratedStack";
inline constexpr std::size_t kMaxGeneratedStackLayers = 8;
[[nodiscard]] std::vector<InterpreterInstruction>
CodeShape(std::span<const InterpreterInstruction> code);
[[nodiscard]] std::expected<std::string, std::string>
GenerateStackShader(const StackShape &shape);
}
