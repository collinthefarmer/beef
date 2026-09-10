#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct MaskPreset {
  std::string name;
  std::optional<std::uint32_t> partition;
  std::vector<std::string> bones;
  std::string expression;
  std::vector<std::pair<std::string, SourceKind>> sources;
};

struct MaskPresets {
  std::vector<MaskPreset> presets;
};

inline constexpr std::size_t kMaxPresets = 256;
inline constexpr std::size_t kMaxPresetBones = 64;
inline constexpr std::size_t kMaxPresetSources = 16;

[[nodiscard]] std::expected<MaskPresets, std::string>
ParsePresets(std::string_view a_json);
}
