#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct MaskPreset {
  std::string name;
  std::optional<BipedSlot> partition;
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

struct PresetsLoadResult {
  std::optional<MaskPresets> presets;
  std::vector<Diagnostic> diagnostics;
  [[nodiscard]] bool HasErrors() const noexcept;
};

[[nodiscard]] std::string PresetWhere(std::string_view a_preset);
[[nodiscard]] PresetsLoadResult ParsePresets(std::string_view a_json);
[[nodiscard]] std::string SerializePresets(const MaskPresets &a_presets);
}
