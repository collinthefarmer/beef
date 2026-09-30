// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldInlining.h"
#include "planners/ProgramReference.h"
#include "test_support.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
using Random = std::mt19937;

RenderBindingResolver PerGeometryMesh() {
  return [](const TextureValue &value, GeometryId geometry) {
    return ValueBindings{
        [instance = value.instance, geometry](const ExternalSource &source)
            -> std::expected<std::string, std::string> {
          if (Is<GeometryInput>(source))
            return "mesh " + std::to_string(IndexOf(geometry));
          return std::to_string(instance);
        },
        [instance = value.instance](NodeId node) {
          return std::to_string(instance) + ":" + std::to_string(node);
        }};
  };
}

struct Texel {
  std::map<std::size_t, Vec3> inputs;
  std::map<std::size_t, Vec3> external;
  std::map<std::size_t, LookupTable> lookups;
};

Vec3 RandomVec(Random &random) {
  std::uniform_real_distribution<float> value{-0.5f, 1.5f};
  return {value(random), value(random), value(random)};
}

Texel RandomTexel(const RenderPlan &plan, Random &random) {
  Texel texel;
  for (std::size_t i = 0; i < plan.inputs.size(); ++i)
    texel.inputs[i] = RandomVec(random);
  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    texel.external[i] = RandomVec(random);
    LookupTable table{};
    std::uniform_real_distribution<float> sample{0.0f, 1.0f};
    for (auto &entry : table)
      entry = sample(random);
    texel.lookups[i] = table;
  }
  return texel;
}

struct Evaluator {
  const RenderPlan &plan;
  const Texel &texel;
  std::map<std::size_t, Vec3> memo;

  Vec3 Raw(RenderValueRef ref) {
    if (const auto *input = Get<RenderInputRef>(ref))
      return texel.inputs.at(input->input);
    const auto step = Get<StepOutputRef>(ref)->step;
    if (const auto found = memo.find(step); found != memo.end())
      return found->second;
    Vec3 value = texel.external.at(step);
    if (const auto program = AsProgramLike(plan, plan.steps[step].kind)) {
      std::vector<Vec3> inputs;
      for (std::size_t k = 0; k < program->inputs.size(); ++k)
        inputs.push_back(Is<ProgramTextureInput>(program->program.Inputs()[k])
                             ? Stored(program->inputs[k])
                             : Raw(program->inputs[k]));
      std::vector<LookupTable> lookups;
      for (const auto &lookup : program->lookups)
        lookups.push_back(texel.lookups.at(Get<StepOutputRef>(lookup)->step));
      value = EvaluateProgramOnCpu(program->program, {inputs, lookups});
      if (program->program.ResultType() == ValueType::kScalar)
        value = {value.x, value.x, value.x};
    }
    memo.emplace(step, value);
    return value;
  }
  Vec3 Stored(RenderValueRef ref) {
    if (Get<RenderInputRef>(ref))
      return QuantizeUnorm8(Raw(ref));
    return QuantizeUnorm8(Raw(ref));
  }
};

bool SameBits(Vec3 a, Vec3 b) {
  const auto same = [](float x, float y) {
    return (std::isnan(x) && std::isnan(y)) ||
           std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
  };
  return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}

std::vector<Vec3> FieldValues(Evaluator &evaluator,
                              const CompositeStackStep &stack,
                              std::span<const std::size_t> read) {
  std::vector<const LayerField *> fields;
  for (const auto field : read)
    if (field < stack.fields.size())
      fields.push_back(&stack.fields[field]);
  const auto packed = PackLayerFields(fields);
  if (!packed)
    return {};
  std::vector<Vec3> inputs;
  for (std::size_t k = 0; k < packed->inputs.size(); ++k)
    inputs.push_back(Is<ProgramTextureInput>(packed->pack.inputs[k])
                         ? evaluator.Stored(packed->inputs[k])
                         : evaluator.Raw(packed->inputs[k]));
  std::vector<LookupTable> lookups;
  for (const auto &lookup : packed->lookups)
    lookups.push_back(
        evaluator.texel.lookups.at(Get<StepOutputRef>(lookup)->step));
  std::vector<Vec3> values;
  for (const auto &segment : packed->pack.segments)
    values.push_back(EvaluateProgramOnCpu(
        std::span{packed->pack.code}.subspan(segment.first, segment.count),
        {inputs, lookups}));
  return values;
}

RenderValueRef LoweredRead(const LayerRead &read) {
  const auto *value = Get<RenderValueRef>(read);
  return value ? *value : RenderValueRef{RenderInputRef{0}};
}

std::size_t CompareStacks(const RenderPlan &original, const RenderPlan &inlined,
                          Random &random, int texels) {
  std::size_t differing = 0;
  for (int t = 0; t < texels; ++t) {
    const auto texel = RandomTexel(original, random);
    Evaluator before{original, texel, {}};
    Evaluator after{inlined, texel, {}};
    for (std::size_t s = 0; s < original.steps.size(); ++s) {
      const auto *stack = Get<CompositeStackStep>(original.steps[s].kind);
      const auto *inlinedStack = Get<CompositeStackStep>(inlined.steps[s].kind);
      if (!stack || !inlinedStack ||
          stack->layers.size() != inlinedStack->layers.size()) {
        differing += stack ? 1 : 0;
        continue;
      }
      std::vector<std::size_t> visible;
      std::vector<const PlannedLayer *> shown;
      for (std::size_t l = 0; l < inlinedStack->layers.size(); ++l)
        if (std::bernoulli_distribution{0.7}(random)) {
          visible.push_back(l);
          shown.push_back(&inlinedStack->layers[l]);
        }
      const auto read = VisibleLayerFields(shown);
      const auto fields = FieldValues(after, *inlinedStack, read);
      const auto inlinedOperand =
          [&](const LayerRead &layerRead) -> std::optional<Vec3> {
        if (const auto *value = Get<RenderValueRef>(layerRead))
          return after.Stored(*value);
        const auto segment = SegmentIndexOf(read, layerRead);
        if (!segment || *segment >= fields.size())
          return std::nullopt;
        return fields[*segment];
      };
      for (const auto l : visible) {
        const auto &layer = stack->layers[l];
        const auto &inlinedLayer = inlinedStack->layers[l];
        std::vector<std::pair<RenderValueRef, std::optional<Vec3>>> operands{
            {LoweredRead(layer.source), inlinedOperand(inlinedLayer.source)},
            {layer.opacity, after.Stored(layer.opacity)}};
        if (layer.mask && inlinedLayer.mask)
          operands.emplace_back(LoweredRead(*layer.mask),
                                inlinedOperand(*inlinedLayer.mask));
        if (layer.color)
          operands.emplace_back(*layer.color, after.Stored(*layer.color));
        for (const auto &[operand, value] : operands)
          if (!value || !SameBits(before.Stored(operand), *value))
            ++differing;
      }
    }
  }
  return differing;
}

std::string Expression(Random &random, std::span<const std::string> leaves,
                       int depth) {
  const auto pick = [&](std::size_t n) {
    return std::uniform_int_distribution<std::size_t>{0, n - 1}(random);
  };
  if (depth <= 0 || pick(4) == 0)
    return leaves[pick(leaves.size())];
  const auto a = Expression(random, leaves, depth - 1);
  const auto b = Expression(random, leaves, depth - 1);
  switch (pick(8)) {
  case 0:
    return "(" + a + " + " + b + ")";
  case 1:
    return "(" + a + " * " + b + ")";
  case 2:
    return "saturate(" + a + " - " + b + ")";
  case 3:
    return "max(" + a + ", " + b + ")";
  case 4:
    return "sin(" + a + ")";
  case 5:
    return "lerp(" + a + ", " + b + ", 0.3)";
  case 6:
    return "frac(" + a + ")";
  default:
    return "smoothstep(0, 1, " + a + ")";
  }
}
}

int main() {
  Random random{5};
  std::size_t chains = 0, programsInlined = 0, layerFieldReads = 0,
              differing = 0;
  for (int trial = 0; trial < 150; ++trial) {
    Recipe recipe;
    recipe.signals = {{"t", ExprSignal{"time * 0.37 + 0.1"}},
                      {"a", ConstantSignal{0.4f}}};
    recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
    const std::array<std::string, 3> base{"@s", "@t", "@a"};
    const std::array<std::string, 3> second{"@m1", "@t", "@s"};
    const std::array<std::string, 3> third{"@m2", "@t", "@m1"};
    recipe.masks = {{"m1", Expression(random, base, 3)},
                    {"m2", "(" + Expression(random, second, 3) + ") + @m1 * 0"},
                    {"m3", "(" + Expression(random, third, 2) + ") + @m2 * 0"},
                    {"m4", "(" + Expression(random, base, 2) + ") + @t * 0"}};
    SurfaceOutput surface;
    Layer layer;
    layer.source = Ref{"m3"};
    if (trial % 3 == 0)
      layer.mask = Ref{"m1"};
    else if (trial % 3 == 1)
      layer.mask = Ref{"m4"};
    surface.stack.push_back(layer);
    recipe.outputs.push_back(surface);
    const auto graph = RecipeGraph::Compile(recipe);
    const std::array requests{RenderStackRequest{
        &graph, &surface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
    const auto plan = LowerRenderPlan({}, requests, PerGeometryMesh());
    if (!plan)
      continue;
    ++chains;
    const auto inlined = InlineFields(*plan);
    programsInlined += inlined.programsInlined;
    layerFieldReads += inlined.layerFieldReads;
    Check(ValidateRenderPlan(inlined.plan).has_value(),
          "a inlined plan is valid");
    differing += CompareStacks(*plan, inlined.plan, random, 10);
  }
  std::printf("field inlining: %zu chains, %zu producers inlined, %zu layer "
              "field reads inlined\n",
              chains, programsInlined, layerFieldReads);
  Check(chains > 100 && programsInlined > 100 && layerFieldReads > 100,
        "the generator fuses many animated chains into their stacks");
  test::Equal(differing, std::size_t{0},
              "every stack operand of an inlined plan equals the lowered one, "
              "bit for bit");

  Recipe shared;
  shared.signals = {{"t", ExprSignal{"time"}}};
  shared.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  shared.masks = {{"m1", "@s * @t"}, {"m2", "@m1 + 0.1"}, {"m3", "@m1 * 2"}};
  SurfaceOutput twoLayers;
  Layer first;
  first.source = Ref{"m2"};
  Layer second;
  second.source = Ref{"m3"};
  twoLayers.stack = {first, second};
  shared.outputs.push_back(twoLayers);
  const auto sharedGraph = RecipeGraph::Compile(shared);
  const std::array sharedRequests{RenderStackRequest{
      &sharedGraph, &twoLayers, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  if (const auto plan =
          LowerRenderPlan({}, sharedRequests, PerGeometryMesh())) {
    const auto inlined = InlineFields(*plan);
    const auto consumers = LiveConsumers(inlined.plan);
    std::size_t liveM1 = 0;
    for (std::size_t s = 0; s < inlined.plan.steps.size(); ++s)
      if (inlined.plan.steps[s].displayName == "mask m1" && consumers[s] >= 1)
        ++liveM1;
    Check(liveM1 == 1,
          "a producer read by two programs stays materialized once");
    test::Equal(CompareStacks(*plan, inlined.plan, random, 20), std::size_t{0},
                "the shared producer plan stays equivalent");
  }

  Recipe twice;
  twice.signals = {{"t", ExprSignal{"time"}}};
  twice.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  twice.masks = {{"m1", "sin(@s * 3 + @t) * 0.5 + 0.5"}};
  SurfaceOutput twiceSurface;
  Layer firstRead;
  firstRead.source = Ref{"m1"};
  Layer secondRead;
  secondRead.source = Ref{"m1"};
  secondRead.mask = Ref{"m1"};
  twiceSurface.stack = {firstRead, secondRead};
  twice.outputs.push_back(twiceSurface);
  const auto twiceGraph = RecipeGraph::Compile(twice);
  const std::array twiceRequests{RenderStackRequest{
      &twiceGraph, &twiceSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  if (const auto plan = LowerRenderPlan({}, twiceRequests, PerGeometryMesh())) {
    const auto inlined = InlineFields(*plan);
    std::size_t stackFields = 0;
    for (const auto &step : inlined.plan.steps)
      if (const auto *stack = Get<CompositeStackStep>(step.kind))
        stackFields += stack->fields.size();
    test::Equal(inlined.layerFieldReads, std::size_t{3},
                "three layer reads take the shared field");
    test::Equal(stackFields, std::size_t{1},
                "a field read by several layers is stored once per stack");
    test::Equal(CompareStacks(*plan, inlined.plan, random, 20), std::size_t{0},
                "the shared layer field plan stays equivalent");
  }

  {
    CompositeStackStep stack;
    PlannedLayer hidden{LayerFieldRef{0}, RenderInputRef{0}, {}, {},
                        Blend::kReplace,  ChannelSet{}};
    PlannedLayer shown{
        RenderInputRef{0}, RenderInputRef{0}, LayerRead{LayerFieldRef{1}}, {},
        Blend::kReplace,   ChannelSet{}};
    stack.layers = {hidden, shown};
    const std::array<const PlannedLayer *, 1> visible{&stack.layers[1]};
    const auto read = VisibleLayerFields(visible);
    Check(read == std::vector<std::size_t>{1},
          "only the fields of visible layers are read");
    Check(SegmentIndexOf(read, LayerFieldRef{1}) ==
              std::optional<std::uint32_t>{0},
          "a visible field takes the first segment of its pack");
    Check(!SegmentIndexOf(read, LayerFieldRef{0}).has_value(),
          "a hidden layer's field has no segment");
  }

  Recipe still;
  still.signals = {{"k", ConstantSignal{0.5f}}};
  still.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  still.masks = {{"m1", "@s * @k"}, {"m2", "@m1 + 0.1"}};
  SurfaceOutput stillSurface;
  Layer stillLayer;
  stillLayer.source = Ref{"m2"};
  stillSurface.stack.push_back(stillLayer);
  still.outputs.push_back(stillSurface);
  const auto stillGraph = RecipeGraph::Compile(still);
  const std::array stillRequests{RenderStackRequest{
      &stillGraph, &stillSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  if (const auto plan = LowerRenderPlan({}, stillRequests, PerGeometryMesh())) {
    const auto inlined = InlineFields(*plan);
    test::Equal(inlined.programsInlined + inlined.layerFieldReads,
                std::size_t{0},
                "a static producer stays materialized and cached");
  }

  const auto folder =
      test::Fixtures().parent_path().parent_path() / "recipes/examples";
  std::vector<Recipe> recipes;
  for (const std::string name :
       {"arcane-circuit", "resonant-ward", "winterglass"})
    if (auto loaded =
            ParseRecipe(test::ReadFile(folder / (name + ".json")), name);
        loaded.recipe)
      recipes.push_back(*loaded.recipe);
  std::vector<std::unique_ptr<RecipeGraph>> graphs;
  for (const auto &recipe : recipes)
    graphs.push_back(
        std::make_unique<RecipeGraph>(RecipeGraph::Compile(recipe)));
  std::vector<RenderStackRequest> demo;
  for (std::size_t g = 0; g < 2; ++g)
    for (std::size_t r = 0; r < recipes.size(); ++r)
      for (std::size_t o = 0; o < recipes[r].outputs.size(); ++o)
        if (const auto *s = Get<SurfaceOutput>(recipes[r].outputs[o]))
          demo.push_back({graphs[r].get(),
                          s,
                          r + 1,
                          PlacementId{static_cast<std::uint32_t>(r)},
                          o,
                          {TextureSize{512}},
                          GeometryId{g}});
  if (const auto plan = LowerRenderPlan({}, demo, PerGeometryMesh())) {
    const auto inlined = InlineFields(*plan);
    std::printf("field inlining: demo plan %zu steps, %zu producers inlined, "
                "%zu layer field reads inlined\n",
                plan->steps.size(), inlined.programsInlined,
                inlined.layerFieldReads);
    Check(ValidateRenderPlan(inlined.plan).has_value(),
          "the inlined demo plan is valid");
    Check(inlined.layerFieldReads > 0,
          "animated demo fields read by several stacks inline into each");
    test::Equal(CompareStacks(*plan, inlined.plan, random, 20), std::size_t{0},
                "the inlined demo plan is equivalent");
  }
  return test::Finish("field inlining");
}
