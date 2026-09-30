// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/StackShader.h"
#include "test_support.h"

#include <cstdlib>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
std::optional<InterpreterProgram> StoredField(std::string_view expression,
                                              float constant) {
  Recipe recipe;
  recipe.signals = {{"a", ConstantSignal{constant}}};
  recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  recipe.masks = {{"m", std::string{expression}}};
  const auto graph = RecipeGraph::Compile(recipe);
  const auto node = graph.FindNodeIndex("m");
  if (!node)
    return std::nullopt;
  const auto program = InterpreterProgram::Compile(graph, {*node, 0});
  if (!program)
    return std::nullopt;
  auto stored = InterpreterProgram::Inline(
      InterpreterProgram::Sample(ValueType::kVec3), 0, *program);
  if (!stored)
    return std::nullopt;
  return *stored;
}

std::optional<StackShape> FieldStack(float constant) {
  const auto source = StoredField("sin(@s * 3 + @a) * 0.5 + 0.5", constant);
  const auto mask = StoredField("saturate(@s - @a * 2)", constant);
  if (!source || !mask)
    return std::nullopt;
  const std::array<const InterpreterProgram *, 2> programs{&*source, &*mask};
  const auto pack = PackInterpreters(programs);
  if (!pack)
    return std::nullopt;
  StackShape shape;
  shape.base = true;
  shape.code = CodeShape(pack->code);
  shape.slots = TextureSlots(pack->inputs);
  shape.segments = pack->segments;
  LayerShape layer;
  layer.source = FieldRead{0};
  layer.channel = 4;
  layer.blend = 2;
  layer.mask = FieldRead{1};
  layer.maskChannel = 4;
  shape.layers = {layer};
  return shape;
}

void MatchGolden(const std::string &name, const std::string &text) {
  const auto golden = test::Fixtures() / "shaders" / (name + ".hlsl");
  if (std::getenv("BEEF_UPDATE")) {
    Check(test::WriteFile(golden, text), "golden " + name + " rewritten");
    return;
  }
  Check(text == test::ReadFile(golden),
        name + " matches its golden (BEEF_UPDATE=1 rewrites it)");
}
}

int main() {
  StackShape textures;
  LayerShape placed;
  placed.source = TextureSourceRead{false};
  placed.channel = 4;
  placed.blend = 1;
  placed.mask = TextureMaskRead{};
  placed.maskChannel = 0;
  LayerShape tint;
  tint.blend = 4;
  tint.channels = 7;
  LayerShape meshSpace;
  meshSpace.source = TextureSourceRead{true};
  textures.layers = {placed, tint, meshSpace};
  const auto texturesText = GenerateStackShader(textures);
  Check(texturesText.has_value(), "a texture stack generates");
  if (texturesText)
    MatchGolden("stack-textures", *texturesText);

  const auto fields = FieldStack(0.25f);
  const auto otherNumbers = FieldStack(0.75f);
  Check(fields && otherNumbers, "the field stack builds");
  if (fields && otherNumbers) {
    Check(*fields == *otherNumbers,
          "stack shapes that differ only in numbers are equal");
    const auto text = GenerateStackShader(*fields);
    Check(text.has_value(), "a field stack generates");
    if (text)
      MatchGolden("stack-fields", *text);
    auto missing = *fields;
    missing.layers[0].mask = FieldRead{7};
    Check(!GenerateStackShader(missing).has_value(),
          "a shape reading a missing field is refused");
  }

  Check(!GenerateStackShader(StackShape{}).has_value(),
        "a shape without layers is refused");
  StackShape tooMany;
  tooMany.layers.resize(kMaxGeneratedStackLayers + 1);
  Check(!GenerateStackShader(tooMany).has_value(),
        "a shape over the layer limit is refused");
  return test::Finish("stack shader");
}
