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

[[nodiscard]] std::optional<std::string> ReadPartition(const json &a_entry,
                                                       MaskPreset &a_preset) {
  if (!a_entry.contains("partition")) {
    return std::nullopt;
  }
  const auto slot = PartitionFrom(a_entry["partition"]);
  if (!slot) {
    return std::format("preset {}: unknown partition", a_preset.name);
  }
  a_preset.partition = *slot;
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> ReadBones(const json &a_entry,
                                                   MaskPreset &a_preset) {
  const auto bones = a_entry.find("bones");
  if (bones == a_entry.end() || !bones->is_array()) {
    return std::nullopt;
  }
  if (bones->size() > kMaxPresetBones) {
    return std::format("preset {}: more than {} bones", a_preset.name,
                       kMaxPresetBones);
  }
  for (const auto &bone : *bones) {
    if (bone.is_string()) {
      a_preset.bones.push_back(bone.get<std::string>());
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> ReadExpression(const json &a_entry,
                                                        MaskPreset &a_preset) {
  const auto expression = a_entry.find("expression");
  if (expression == a_entry.end() || !expression->is_string()) {
    return std::nullopt;
  }
  a_preset.expression = expression->get<std::string>();
  if (a_preset.expression.size() > kMaxExpressionLength) {
    return std::format("preset {}: expression longer than {} characters",
                       a_preset.name, kMaxExpressionLength);
  }
  if (const auto program = Program::Parse(a_preset.expression); !program) {
    return std::format("preset {}: expression: {}", a_preset.name,
                       program.error());
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> ReadSources(const json &a_entry,
                                                     MaskPreset &a_preset) {
  const auto sources = a_entry.find("sources");
  if (sources == a_entry.end() || !sources->is_object()) {
    return std::nullopt;
  }
  if (sources->size() > kMaxPresetSources) {
    return std::format("preset {}: more than {} sources", a_preset.name,
                       kMaxPresetSources);
  }
  for (const auto &[name, definition] : sources->items()) {
    const auto kind = SourceFromJson(definition);
    if (!IsName(name) || !kind) {
      return std::format(
          "preset {}: source '{}' is not a material channel, bake or uv",
          a_preset.name, name);
    }
    a_preset.sources.emplace_back(name, *kind);
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
  if (auto error = ReadPartition(a_entry, preset)) {
    return std::unexpected(std::move(*error));
  }
  if (auto error = ReadBones(a_entry, preset)) {
    return std::unexpected(std::move(*error));
  }
  if (auto error = ReadExpression(a_entry, preset)) {
    return std::unexpected(std::move(*error));
  }
  if (preset.expression.empty() && !preset.partition && preset.bones.empty()) {
    return std::unexpected(std::format(
        "preset {}: needs an expression, a partition or bones", preset.name));
  }
  if (auto error = ReadSources(a_entry, preset)) {
    return std::unexpected(std::move(*error));
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
