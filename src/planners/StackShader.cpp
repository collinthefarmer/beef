// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/StackShader.h"

#include <format>

namespace BetterEnchantmentEffects {
namespace {
std::string FieldName(std::uint32_t segment) {
  return std::format("StackField{}", segment);
}

bool SegmentExists(const StackShape &shape, std::uint32_t segment) {
  return segment < shape.segments.size() &&
         shape.segments[segment].first <= shape.code.size() &&
         shape.segments[segment].count <=
             shape.code.size() - shape.segments[segment].first;
}

std::optional<std::uint32_t> FieldOf(const SourceRead &read) {
  const auto *field = std::get_if<FieldRead>(&read);
  return field ? std::optional{field->segment} : std::nullopt;
}

std::optional<std::uint32_t> FieldOf(const MaskRead &read) {
  const auto *field = std::get_if<FieldRead>(&read);
  return field ? std::optional{field->segment} : std::nullopt;
}

std::string SourceStatement(const LayerShape &layer, std::size_t k) {
  if (const auto segment = FieldOf(layer.source))
    return std::format("\t\ts = float4({}(rawUv), 1);\n", FieldName(*segment));
  if (const auto *texture = std::get_if<TextureSourceRead>(&layer.source))
    return std::format(
        "\t\tfloat2 uv = {};\n\t\ts = StackSource({}, uv, "
        "stackFlags[{}].w);\n",
        texture->meshSpace
            ? std::string{"rawUv"}
            : std::format(
                  "PlaceUv(rawUv, stackOffsetScale[{}], stackFlags[{}])", k, k),
        k, k);
  return {};
}

std::string MaskStatement(const LayerShape &layer, std::size_t k) {
  if (const auto segment = FieldOf(layer.mask))
    return std::format("\t\tm = float4({}(rawUv), 1);\n", FieldName(*segment));
  if (std::holds_alternative<TextureMaskRead>(layer.mask))
    return std::format("\t\tm = StackMap({}, rawUv);\n", k);
  return {};
}
}

std::vector<InterpreterInstruction>
CodeShape(std::span<const InterpreterInstruction> code) {
  std::vector<InterpreterInstruction> shape(code.begin(), code.end());
  for (auto &instruction : shape)
    instruction.number = 0;
  return shape;
}

std::expected<std::string, std::string>
GenerateStackShader(const StackShape &shape) {
  if (shape.layers.empty() || shape.layers.size() > kMaxGeneratedStackLayers)
    return std::unexpected("stack shape has no layers or too many");
  std::vector<std::uint32_t> fields;
  for (const auto &layer : shape.layers)
    for (const auto segment : {FieldOf(layer.source), FieldOf(layer.mask)})
      if (segment) {
        if (!SegmentExists(shape, *segment))
          return std::unexpected("stack shape reads a missing field");
        if (std::ranges::find(fields, *segment) == fields.end())
          fields.push_back(*segment);
      }
  std::string text;
  for (const auto segment : fields) {
    const auto &range = shape.segments[segment];
    text +=
        ProgramFunction(FieldName(segment),
                        std::span{shape.code}.subspan(range.first, range.count),
                        range.first, shape.slots);
  }
  text +=
      std::format("float4 {}(VSOut i) : SV_Target\n{{\n", kGeneratedStackEntry);
  text += "\tfloat2 rawUv = i.uv;\n";
  text += shape.base
              ? "\tfloat4 below = stackBase.SampleLevel(samp, rawUv, 0);\n"
              : "\tfloat4 below = float4(0, 0, 0, 0);\n";
  for (std::size_t k = 0; k < shape.layers.size(); ++k) {
    const auto &layer = shape.layers[k];
    const bool hasSource =
        !std::holds_alternative<std::monostate>(layer.source);
    const bool hasMask = !std::holds_alternative<std::monostate>(layer.mask);
    text += "\t{\n\t\tfloat4 s = 0;\n\t\tfloat4 m = 0;\n";
    text += SourceStatement(layer, k);
    text += std::format(
        "\t\tfloat3 value = LayerValue(stackColor[{}], {}, s, {});\n", k,
        hasSource ? "true" : "false", layer.channel);
    text += MaskStatement(layer, k);
    text += std::format(
        "\t\tfloat4 result = ComposeLayer(below, value, stackLayer[{}].w, {}, "
        "m, {}, {}, {});\n",
        k, hasMask ? "true" : "false", hasMask ? layer.maskChannel : 0u,
        layer.blend, layer.channels);
    text += k + 1 < shape.layers.size() ? "\t\tbelow = Unorm8(result);\n"
                                        : "\t\tbelow = result;\n";
    text += "\t}\n";
  }
  text += "\treturn below;\n}\n";
  return text;
}
}
