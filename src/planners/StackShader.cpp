// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/StackShader.h"

#include <format>

namespace BetterEnchantmentEffects {
namespace {
std::string FieldName(std::uint32_t segment) {
  return std::format("StackField{}", segment);
}

std::optional<std::uint32_t> SegmentOf(const SegmentRead *read) {
  return read ? std::optional{read->segment} : std::nullopt;
}

std::optional<std::uint32_t> FieldOf(const SourceRead &read) {
  return SegmentOf(std::get_if<SegmentRead>(&read));
}

std::optional<std::uint32_t> FieldOf(const MaskRead &read) {
  return SegmentOf(std::get_if<SegmentRead>(&read));
}

ProgramSegment FieldSegment(const StackShape &shape,
                            std::optional<std::uint32_t> segment) {
  if (!segment)
    return {};
  return SegmentAt(shape.segments, shape.code.size(), *segment)
      .value_or(ProgramSegment{});
}

std::string SourceStatement(const LayerShape &layer, std::size_t k) {
  if (const auto segment = FieldOf(layer.source))
    return std::format("\t\ts = float4({}(rawUv), 1);\n", FieldName(*segment));
  if (const auto *texture = std::get_if<SourceTexture>(&layer.source))
    return std::format(
        "\t\tfloat2 uv = {};\n\t\ts = SampleStackSource({}, uv, "
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
  if (std::holds_alternative<MaskTexture>(layer.mask))
    return std::format("\t\tm = SampleStackMask({}, rawUv);\n", k);
  return {};
}
}

std::vector<ProgramInstruction>
CodeWithoutNumbers(std::span<const ProgramInstruction> code) {
  std::vector<ProgramInstruction> shape(code.begin(), code.end());
  for (auto &instruction : shape)
    instruction.number = 0;
  return shape;
}

bool HasSource(const LayerShape &layer) {
  return !std::holds_alternative<std::monostate>(layer.source);
}

bool HasMask(const LayerShape &layer) {
  return !std::holds_alternative<std::monostate>(layer.mask);
}

ProgramSegment SourceSegment(const StackShape &shape, const LayerShape &layer) {
  return FieldSegment(shape, FieldOf(layer.source));
}

ProgramSegment MaskSegment(const StackShape &shape, const LayerShape &layer) {
  return FieldSegment(shape, FieldOf(layer.mask));
}

std::expected<void, std::string> CheckStackShape(const StackShape &shape) {
  if (shape.layers.empty() || shape.layers.size() > kMaxStackLayers)
    return std::unexpected("stack shape has no layers or too many");
  if (shape.code.size() > kProgramInstructions)
    return std::unexpected("stack shape code exceeds the program limit");
  for (const auto &layer : shape.layers)
    for (const auto segment : {FieldOf(layer.source), FieldOf(layer.mask)})
      if (segment && !SegmentAt(shape.segments, shape.code.size(), *segment))
        return std::unexpected("stack shape reads a missing field");
  return {};
}

std::expected<std::string, std::string>
GenerateStackShader(const StackShape &shape) {
  if (const auto checked = CheckStackShape(shape); !checked)
    return std::unexpected(checked.error());
  std::vector<std::pair<std::uint32_t, ProgramSegment>> fields;
  for (const auto &layer : shape.layers)
    for (const auto segment : {FieldOf(layer.source), FieldOf(layer.mask)})
      if (segment &&
          std::ranges::find(fields, *segment,
                            &std::pair<std::uint32_t, ProgramSegment>::first) ==
              fields.end())
        fields.emplace_back(*segment, FieldSegment(shape, segment));
  std::string text;
  for (const auto &[segment, range] : fields) {
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
    const bool hasSource = HasSource(layer);
    const bool hasMask = HasMask(layer);
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
