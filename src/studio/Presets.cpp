#include "studio/Presets.h"

#include "recipe/Binders.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"

#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
using PresetSource = std::pair<std::string, SourceKind>;

std::string PresetLabel(const json &a_entry, std::size_t a_index) {
  std::vector<Diagnostic> ignored;
  Reader head(a_entry, Reporter{ignored, ""});
  const std::string name = head.String("name").value_or(std::string{});
  return name.empty() ? std::to_string(a_index) : name;
}

void ReadName(Reader &a_r, MaskPreset &a_preset) {
  a_preset.name = a_r.Required("name");
  if (!a_preset.name.empty() && !IsName(a_preset.name)) {
    a_r.Context().Error(
        std::format("'name' must be an identifier (letters, digits, '_'): '{}'",
                    a_preset.name));
    a_preset.name.clear();
  }
}

void ReadExpression(Reader &a_r, MaskPreset &a_preset) {
  const auto expression = a_r.String("expression");
  if (!expression) {
    return;
  }
  if (expression->size() > kMaxExpressionLength) {
    a_r.Context().Error(std::format("'expression' is longer than {} characters",
                                    kMaxExpressionLength));
    return;
  }
  if (const auto program = Program::Parse(*expression); !program) {
    a_r.Context().Error(std::format("'expression': {}", program.error()));
    return;
  }
  a_preset.expression = *expression;
}

std::optional<PresetSource> SourceFrom(const std::string &a_name,
                                       const json &a_value,
                                       const Reporter &a_ctx) {
  if (!IsName(a_name)) {
    a_ctx.Error(std::format(
        "a source name is an identifier (letters, digits, '_'): '{}'", a_name));
    return std::nullopt;
  }
  Reader r(a_value, a_ctx);
  auto kind = ParseSourceKind(r);
  if (!kind) {
    return std::nullopt;
  }
  r.Finish();
  return PresetSource{a_name, std::move(*kind)};
}

void ReadSources(Reader &a_r, MaskPreset &a_preset) {
  const std::string where = a_r.Context().where;
  NamedRows(
      a_r, "sources",
      [&](const std::string &a_name) {
        return std::format("{} {}", where, SourceWhere(a_name));
      },
      a_preset.sources, SourceFrom);
  if (a_preset.sources.size() > kMaxPresetSources) {
    a_r.Context().Error(std::format("more than {} sources", kMaxPresetSources));
    a_preset.sources.resize(kMaxPresetSources);
  }
}

std::optional<MaskPreset> PresetFrom(const json &a_entry,
                                     const Reporter &a_ctx) {
  MaskPreset preset;
  Reader r(a_entry, a_ctx);
  ReadName(r, preset);
  preset.partition = r.BipedSlot("partition");
  preset.bones =
      r.Strings("bones", kMaxPresetBones).value_or(std::vector<std::string>{});
  ReadExpression(r, preset);
  ReadSources(r, preset);
  r.Finish();
  if (preset.name.empty()) {
    return std::nullopt;
  }
  if (preset.expression.empty() && !preset.partition && preset.bones.empty()) {
    a_ctx.Error("needs an expression, a partition or bones");
    return std::nullopt;
  }
  return preset;
}

json PresetToJson(const MaskPreset &a_preset) {
  json o = json::object();
  Writer w{o};
  w.WriteText("name", a_preset.name);
  if (a_preset.partition) {
    w.Set("partition", BipedSlotToJson(*a_preset.partition));
  }
  w.WriteStringsIf("bones", a_preset.bones);
  w.WriteTextIf("expression", a_preset.expression);
  if (!a_preset.sources.empty()) {
    json sources = json::object();
    for (const auto &[name, kind] : a_preset.sources) {
      sources[name] = SourceKindToJson(kind);
    }
    w.Set("sources", std::move(sources));
  }
  return o;
}
}

bool PresetsLoadResult::HasErrors() const noexcept {
  return !presets || BetterEnchantmentEffects::HasErrors(diagnostics);
}

std::string PresetWhere(std::string_view a_preset) {
  return std::format("preset {}", a_preset);
}

PresetsLoadResult ParsePresets(std::string_view a_json) {
  PresetsLoadResult result;
  const Reporter fileCtx{result.diagnostics, "file"};
  const std::optional<json> root = ParseObjectDocument(a_json, fileCtx);
  if (!root) {
    return result;
  }
  MaskPresets presets;
  const Reporter ctx{result.diagnostics, "presets"};
  Reader r(*root, ctx);
  if (const json *entries = r.Child("presets")) {
    if (!entries->is_array()) {
      ctx.Error("'presets' must be an array");
    } else {
      ReadRows(
          *entries, "presets", ctx, presets.presets,
          [&](const json &a_entry, std::size_t a_index) {
            return PresetFrom(
                a_entry, ctx.At(PresetWhere(PresetLabel(a_entry, a_index))));
          },
          kMaxPresets);
    }
  }
  r.Finish();
  result.presets = std::move(presets);
  return result;
}

std::string SerializePresets(const MaskPresets &a_presets) {
  json root = json::object();
  json entries = json::array();
  for (const MaskPreset &preset : a_presets.presets) {
    entries.push_back(PresetToJson(preset));
  }
  root["presets"] = std::move(entries);
  return DumpDocument(root);
}
}
