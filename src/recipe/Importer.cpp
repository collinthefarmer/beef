// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Importer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
using json = nlohmann::json;

Vec3 ColorFrom(const json &a_array) {
  if (!a_array.is_array() || a_array.size() != 3) {
    return {};
  }
  for (const auto &part : a_array) {
    if (!part.is_number()) {
      return {};
    }
  }
  return Vec3{a_array[0].get<float>() / 255.0f,
              a_array[1].get<float>() / 255.0f,
              a_array[2].get<float>() / 255.0f};
}

float FloatAt(const json &a_object, const char *a_field,
              float a_default = 0.0f) {
  return a_object.contains(a_field) && a_object.at(a_field).is_number()
             ? a_object.at(a_field).get<float>()
             : a_default;
}

std::string TextAt(const json &a_object, const char *a_field) {
  return a_object.contains(a_field) && a_object.at(a_field).is_string()
             ? a_object.at(a_field).get<std::string>()
             : std::string{};
}

float Peak(const Vec3 &a_color) noexcept {
  return std::max({a_color.x, a_color.y, a_color.z});
}

float Chroma(const Vec3 &a_color) noexcept {
  const float hi = Peak(a_color);
  const float lo = std::min({a_color.x, a_color.y, a_color.z});
  return hi <= 1e-4f ? 0.0f : (hi - lo) / hi;
}

Vec3 NormalizeHue(const Vec3 &a_color) noexcept {
  const float hi = Peak(a_color);
  if (hi <= 1e-4f) {
    return a_color;
  }
  return Vec3{a_color.x / hi, a_color.y / hi, a_color.z / hi};
}

Vec3 ResolveEmissiveColor(const Vec3 &a_fill, const Vec3 &a_edge) noexcept {
  const float fillPeak = Peak(a_fill);
  const float brightness = fillPeak <= 0.02f ? 1.0f : fillPeak;

  if (Chroma(a_edge) <= 0.05f) {
    return fillPeak <= 0.02f ? Vec3{1.0f, 1.0f, 1.0f} : a_fill;
  }
  const float edgePeak = Peak(a_edge);
  if (edgePeak <= 1e-4f) {
    return a_fill;
  }
  return Vec3{a_edge.x / edgePeak * brightness,
              a_edge.y / edgePeak * brightness,
              a_edge.z / edgePeak * brightness};
}

Vec3 EmissiveHue(const Vec3 &a_fill, const Vec3 &a_edge) noexcept {
  return NormalizeHue(ResolveEmissiveColor(a_fill, a_edge));
}

inline constexpr std::string_view kImporter = "BetterEnchantmentEffects 0.1.0";

struct ImportFact {
  std::string_view name;
  Value (*value)(const EffectShaderRecord &);
};

constexpr std::array kImportFacts{
    ImportFact{"importHasFill",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.fillTexture.empty() ? 0.0f : 1.0f;
               }},
    ImportFact{"importHue",
               [](const EffectShaderRecord &a_r) -> Value {
                 return EmissiveHue(a_r.params.colorKeys[0],
                                    a_r.params.edgeColor);
               }},
    ImportFact{"importBrightness",
               [](const EffectShaderRecord &a_r) -> Value {
                 return Peak(a_r.params.colorKeys[0]);
               }},
    ImportFact{"importChroma",
               [](const EffectShaderRecord &a_r) -> Value {
                 return Chroma(a_r.params.edgeColor);
               }},
    ImportFact{"importFillBase",
               [](const EffectShaderRecord &a_r) -> Value {
                 return Efsh::BaselineAlpha(a_r.params.fill);
               }},
    ImportFact{"importEdgeBase",
               [](const EffectShaderRecord &a_r) -> Value {
                 return Efsh::BaselineAlpha(a_r.params.edge);
               }},
    ImportFact{"importFillPulseAmp",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.fill.pulseAmplitude;
               }},
    ImportFact{"importFillPulseFreq",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.fill.pulseFrequency;
               }},
    ImportFact{"importFillFadeIn",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.fill.fadeInTime;
               }},
    ImportFact{"importEdgePulseAmp",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.edge.pulseAmplitude;
               }},
    ImportFact{"importEdgePulseFreq",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.edge.pulseFrequency;
               }},
    ImportFact{"importEdgeFadeIn",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.edge.fadeInTime;
               }},
    ImportFact{"importScrollU",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.animationSpeedU;
               }},
    ImportFact{"importScrollV",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.animationSpeedV;
               }},
    ImportFact{
        "importTileU",
        [](const EffectShaderRecord &a_r) -> Value { return a_r.tileU; }},
    ImportFact{
        "importTileV",
        [](const EffectShaderRecord &a_r) -> Value { return a_r.tileV; }},
    ImportFact{"importEdgeColor",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.edgeColor;
               }},
    ImportFact{"importColorKey1",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.colorKeys[0];
               }},
    ImportFact{"importColorKey2",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.colorKeys[1];
               }},
    ImportFact{"importColorKey3",
               [](const EffectShaderRecord &a_r) -> Value {
                 return a_r.params.colorKeys[2];
               }},
};

std::optional<Value> ImportValueFor(std::string_view a_name,
                                    const EffectShaderRecord &a_record) {
  for (const ImportFact &fact : kImportFacts) {
    if (fact.name == a_name) {
      return fact.value(a_record);
    }
  }
  return std::nullopt;
}
}

FormRef EffectShaderRecord::Reference() const {
  return FormRef::From(editorId.empty() ? key.ToString() : editorId);
}

std::expected<EffectShaderRecord, std::string>
ParseEffectShaderRecord(std::string_view a_json) {
  const json root = json::parse(a_json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    return std::unexpected("not a JSON object");
  }
  EffectShaderRecord r;
  const std::optional<FormKey> key = FormKey::Parse(TextAt(root, "formKey"));
  if (!key) {
    return std::unexpected(
        "'formKey' is missing or malformed (expected 0x<id>~<plugin>)");
  }
  r.key = *key;
  r.editorId = TextAt(root, "editorId");
  r.fillTexture = TextAt(root, "fillTexture");

  Efsh::EffectParams &p = r.params;
  p.colorKeys = {ColorFrom(root.value("fillColorKey1", json::array())),
                 ColorFrom(root.value("fillColorKey2", json::array())),
                 ColorFrom(root.value("fillColorKey3", json::array()))};
  p.colorKeyTimes = {FloatAt(root, "fillColorKey1Time"),
                     FloatAt(root, "fillColorKey2Time"),
                     FloatAt(root, "fillColorKey3Time")};
  p.colorKeyScales = {FloatAt(root, "fillColorKey1Scale", 1.0f),
                      FloatAt(root, "fillColorKey2Scale", 1.0f),
                      FloatAt(root, "fillColorKey3Scale", 1.0f)};
  p.colorScale = FloatAt(root, "colorScale", 1.0f);
  p.fill.fullAlphaRatio = FloatAt(root, "fillFullAlphaRatio", 1.0f);
  p.fill.persistentAlphaRatio = FloatAt(root, "fillPersistentAlphaRatio", 1.0f);
  p.fill.pulseAmplitude = FloatAt(root, "fillAlphaPulseAmplitude");
  p.fill.pulseFrequency = FloatAt(root, "fillAlphaPulseFrequency");
  p.fill.fadeInTime = FloatAt(root, "fillAlphaFadeInTime");
  p.animationSpeedU = FloatAt(root, "fillTextureAnimationSpeedU");
  p.animationSpeedV = FloatAt(root, "fillTextureAnimationSpeedV");
  p.edgeColor = ColorFrom(root.value("edgeColor", json::array()));
  p.edge.fullAlphaRatio = FloatAt(root, "edgeFullAlphaRatio", 1.0f);
  p.edge.persistentAlphaRatio = FloatAt(root, "edgePersistentAlphaRatio", 1.0f);
  p.edge.pulseAmplitude = FloatAt(root, "edgeAlphaPulseAmplitude");
  p.edge.pulseFrequency = FloatAt(root, "edgeAlphaPulseFrequency");
  p.edge.fadeInTime = FloatAt(root, "edgeAlphaFadeInTime");
  r.tileU = FloatAt(root, "fillTextureScaleU", 1.0f);
  r.tileV = FloatAt(root, "fillTextureScaleV", 1.0f);
  return r;
}

std::string RecipeIdFor(const EffectShaderRecord &a_record) {
  if (!a_record.editorId.empty()) {
    return a_record.editorId;
  }
  std::string stem = a_record.key.file;
  if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
    stem.erase(dot);
  }
  return std::format("{}-{:X}", stem, a_record.key.localId);
}

std::string_view ImportTemplateId(const EffectShaderRecord &a_record) noexcept {
  return a_record.fillTexture.empty() ? kImportTemplateIds[1]
                                      : kImportTemplateIds[0];
}

std::span<const std::string_view> ImportSignalNames() noexcept {
  static const auto names = [] {
    std::array<std::string_view, kImportFacts.size()> out{};
    for (std::size_t i = 0; i < kImportFacts.size(); ++i) {
      out[i] = kImportFacts[i].name;
    }
    return out;
  }();
  return names;
}

Recipe ImportEffectShader(const EffectShaderRecord &a_record,
                          const Recipe &a_template) {
  const FormRef reference = a_record.Reference();

  Recipe r = a_template;
  r.id = RecipeIdFor(a_record);
  r.metadata.name =
      a_record.editorId.empty() ? a_record.key.ToString() : a_record.editorId;
  r.metadata.description =
      std::format("Imported from effect shader {} with the {} template.",
                  reference.text, a_template.id);
  r.metadata.imported = std::string{kImporter};
  r.keys.clear();
  r.keys.push_back(RecipeKey{KeyKind::kEffectShader, reference});

  for (Signal &signal : r.signals) {
    if (auto *efsh = Get<EfshSignal>(signal.kind)) {
      efsh->record = reference;
    } else if (auto *constant = Get<ConstantSignal>(signal.kind)) {
      if (const std::optional<Value> value =
              ImportValueFor(signal.name, a_record)) {
        constant->value = *value;
      }
    }
  }

  for (Source &source : r.sources) {
    auto *image = Get<ImageSource>(source.kind);
    if (!image || image->path != kFillTextureToken) {
      continue;
    }
    image->path = a_record.fillTexture;
    image->tile = Vec2Param{std::array<Param, 2>{
        std::max(a_record.tileU, 0.01f), std::max(a_record.tileV, 0.01f)}};
  }
  return r;
}
}
