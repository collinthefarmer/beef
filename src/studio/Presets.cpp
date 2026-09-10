#include "studio/Presets.h"

#include "recipe/Expression.h"
#include "recipe/Recipe.h"

#include <nlohmann/json.hpp>

#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
using json = nlohmann::ordered_json;

[[nodiscard]] std::optional<SourceKind> SourceFromJson(const json &a_value) {
  if (!a_value.is_object() || a_value.size() != 1) {
    return std::nullopt;
  }
  const auto first = a_value.begin();
  const std::string &key = first.key();
  const json &value = first.value();
  if (key == "material" && value.is_string()) {
    const auto channel = ParseMaterialChannel(value.get<std::string>());
    return channel ? std::optional<SourceKind>{MaterialSource{*channel}}
                   : std::nullopt;
  }
  if (key == "bake" && value.is_string()) {
    const auto bake = DefaultBakeKind(value.get<std::string>());
    return bake ? std::optional<SourceKind>{BakeSource{*bake}} : std::nullopt;
  }
  if (key == "uv" && value.is_string()) {
    const auto axis = ParseUvAxis(value.get<std::string>());
    return axis ? std::optional<SourceKind>{UvSource{*axis}} : std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> PartitionFrom(const json &a_value) {
  if (a_value.is_string()) {
    return BipedSlotFromName(a_value.get<std::string>());
  }
  if (a_value.is_number_unsigned()) {
    return a_value.get<std::uint32_t>();
  }
  return std::nullopt;
}

[[nodiscard]] std::expected<MaskPreset, std::string>
PresetFrom(const json &a_entry) {
  if (!a_entry.is_object() || !a_entry.contains("name") ||
      !a_entry["name"].is_string() ||
      !IsName(a_entry["name"].get<std::string>())) {
    return std::unexpected("a preset needs a name");
  }
  MaskPreset preset;
  preset.name = a_entry["name"].get<std::string>();
  if (a_entry.contains("partition")) {
    const auto slot = PartitionFrom(a_entry["partition"]);
    if (!slot) {
      return std::unexpected(
          std::format("preset {}: unknown partition", preset.name));
    }
    preset.partition = *slot;
  }
  if (const auto bones = a_entry.find("bones");
      bones != a_entry.end() && bones->is_array()) {
    if (bones->size() > kMaxPresetBones) {
      return std::unexpected(std::format("preset {}: more than {} bones",
                                         preset.name, kMaxPresetBones));
    }
    for (const auto &bone : *bones) {
      if (bone.is_string()) {
        preset.bones.push_back(bone.get<std::string>());
      }
    }
  }
  if (const auto expression = a_entry.find("expression");
      expression != a_entry.end() && expression->is_string()) {
    preset.expression = expression->get<std::string>();
    if (preset.expression.size() > kMaxExpressionLength) {
      return std::unexpected(
          std::format("preset {}: expression longer than {} characters",
                      preset.name, kMaxExpressionLength));
    }
    if (const auto program = Program::Parse(preset.expression); !program) {
      return std::unexpected(std::format("preset {}: expression: {}",
                                         preset.name, program.error()));
    }
  }
  if (preset.expression.empty() && !preset.partition && preset.bones.empty()) {
    return std::unexpected(std::format(
        "preset {}: needs an expression, a partition or bones", preset.name));
  }
  if (const auto sources = a_entry.find("sources");
      sources != a_entry.end() && sources->is_object()) {
    if (sources->size() > kMaxPresetSources) {
      return std::unexpected(std::format("preset {}: more than {} sources",
                                         preset.name, kMaxPresetSources));
    }
    for (const auto &[name, definition] : sources->items()) {
      const auto kind = SourceFromJson(definition);
      if (!IsName(name) || !kind) {
        return std::unexpected(std::format(
            "preset {}: source '{}' is not a material channel, bake or uv",
            preset.name, name));
      }
      preset.sources.emplace_back(name, *kind);
    }
  }
  return preset;
}
}

std::expected<MaskPresets, std::string> ParsePresets(std::string_view a_json) {
  const auto parsed = json::parse(a_json, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return std::unexpected("the preset file is not a JSON object");
  }
  MaskPresets presets;
  const auto entries = parsed.find("presets");
  if (entries == parsed.end() || !entries->is_array()) {
    return presets;
  }
  if (entries->size() > kMaxPresets) {
    return std::unexpected(std::format("more than {} presets", kMaxPresets));
  }
  for (const auto &entry : *entries) {
    auto preset = PresetFrom(entry);
    if (!preset) {
      return std::unexpected(std::move(preset).error());
    }
    presets.presets.push_back(std::move(*preset));
  }
  return presets;
}
}
