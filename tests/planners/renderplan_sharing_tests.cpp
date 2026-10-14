// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldInlining.h"
#include "planners/RenderPlan.h"
#include "test_support.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
RenderBindingResolver Bindings() {
  return [](const TextureValue &value, GeometryId) {
    return ValueBindings{
        [instance = value.instance](
            const ExternalSource &) -> std::expected<std::string, std::string> {
          return std::to_string(instance);
        },
        [instance = value.instance](NodeId node) {
          return std::to_string(instance) + ":" + std::to_string(node);
        }};
  };
}

RenderBindingResolver ActorBindings(std::string a_actor) {
  return [actor = std::move(a_actor)](const TextureValue &value, GeometryId) {
    return ValueBindings{
        [actor, instance = value.instance](
            const ExternalSource &) -> std::expected<std::string, std::string> {
          return actor + ":" + std::to_string(instance);
        },
        [actor, instance = value.instance](NodeId node) {
          return actor + ":" + std::to_string(instance) + ":" +
                 std::to_string(node);
        }};
  };
}

std::vector<std::string> ContentKeys(const RenderPlan &plan) {
  std::vector<std::string> keys;
  for (const auto &step : plan.steps)
    if (step.contentKey)
      keys.push_back(*step.contentKey);
  std::ranges::sort(keys);
  return keys;
}

struct Duplicate {
  std::size_t first = 0, second = 0;
};

std::vector<Duplicate> Duplicates(const RenderPlan &plan) {
  std::vector<Duplicate> duplicates;
  for (std::size_t i = 0; i < plan.steps.size(); ++i)
    for (std::size_t j = i + 1; j < plan.steps.size(); ++j)
      if (plan.steps[i].kind == plan.steps[j].kind)
        duplicates.push_back({i, j});
  return duplicates;
}

struct LoadedRecipe {
  Recipe recipe;
  std::shared_ptr<const RecipeGraph> graph;
};

std::vector<RenderStackRequest>
StackRequests(const std::vector<LoadedRecipe> &recipes,
              std::span<const SideRef> sizes) {
  std::vector<RenderStackRequest> requests;
  for (std::size_t r = 0; r < recipes.size(); ++r)
    for (std::size_t o = 0; o < recipes[r].recipe.outputs.size(); ++o)
      if (const auto *surface =
              Get<SurfaceOutput>(recipes[r].recipe.outputs[o]))
        for (std::size_t s = 0; s < sizes.size(); ++s)
          requests.push_back(
              {recipes[r].graph.get(),
               surface,
               r + 1,
               PlacementId{static_cast<std::uint32_t>(r * sizes.size() + s)},
               o,
               {sizes[s]}});
  return requests;
}

void ContentKeysMarkOnlyStaticTextureSteps(
    const std::vector<LoadedRecipe> &recipes) {
  const std::array size{SideRef{0}};
  const auto plan =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings());
  Check(plan.has_value(), "the demo recipes lower for content keys");
  if (!plan)
    return;
  const std::vector<bool> changing = ChangingSteps(*plan);
  bool onlyStatic = true;
  std::size_t shareable = 0;
  for (std::size_t i = 0; i < plan->steps.size(); ++i) {
    const auto &step = plan->steps[i];
    if (!step.contentKey)
      continue;
    ++shareable;
    onlyStatic = onlyStatic && i < changing.size() && !changing[i] &&
                 !Is<CompositeStackStep>(step.kind) &&
                 RequirementsOf(step.kind).has_value();
  }
  Check(onlyStatic, "only static, non-stack texture steps carry content keys");
  Check(shareable > 0, "the demo recipes have shareable steps");
  const auto again =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings());
  Check(again && ContentKeys(*again) == ContentKeys(*plan),
        "identical inputs give identical content keys");
  const auto first =
      BuildRenderPlan({}, StackRequests(recipes, size), ActorBindings("a"));
  const auto second =
      BuildRenderPlan({}, StackRequests(recipes, size), ActorBindings("b"));
  Check(first && second, "per-actor bindings lower");
  if (first && second) {
    std::vector<std::string> common;
    const auto a = ContentKeys(*first), b = ContentKeys(*second);
    std::ranges::set_intersection(a, b, std::back_inserter(common));
    Check(common.empty(), "inputs that differ per actor never share a key");
  }
}

void RandMasksAreStatic() {
  Recipe recipe;
  recipe.masks = {{"pick", "rand()"},
                  {"magnitude", "@strength * 0.5"},
                  {"pulse", "sin(time)"}};
  recipe.signals = {
      {"strength", EnchantmentSignal{EnchantmentField::kMagnitude}}};
  const RecipeGraph graph = RecipeGraph::Compile(recipe);
  const auto changing = [&](std::string_view a_name) {
    const std::optional<std::size_t> node = graph.FindNodeIndex(a_name);
    return node && graph.Changing(*node);
  };
  Check(graph.FindNodeIndex("pick") && !changing("pick"),
        "a rand() mask is static");
  Check(graph.FindNodeIndex("magnitude") && !changing("magnitude"),
        "a mask over an enchantment field is static");
  Check(changing("pulse"), "a mask over time still changes");
  const std::vector<ExternalSource> sources = StaticExternalSources(graph);
  Check(std::ranges::any_of(sources,
                            [](const ExternalSource &a_source) {
                              return Is<EnchantmentSignal>(a_source);
                            }),
        "enchantment fields are static external inputs");
}

void CacheKeysCarryOperandValues() {
  const std::vector<Value> origin{Vec3{1.0f, 2.0f, 3.0f}};
  const std::vector<Value> moved{Vec3{1.0f, 2.0f, 3.5f}};
  Check(CacheKeyFor("bake", origin) == CacheKeyFor("bake", origin),
        "equal operands give an equal cache key");
  Check(CacheKeyFor("bake", origin) != CacheKeyFor("bake", moved),
        "a moved node position gives a different cache key");
  Check(CacheKeyFor("bake", std::vector<Value>{0.0f}) !=
            CacheKeyFor("bake", std::vector<Value>{Vec2{0.0f, 0.0f}}),
        "operand shapes are part of the cache key");
  Check(CacheKeyFor("bake", {}) == std::optional<std::string>{"bake"},
        "a step without numeric operands keeps its cache key");
  const std::vector<Value> many(kMaxCacheKeyBytes / 8, Value{0.0f});
  Check(!CacheKeyFor("bake", many).has_value(),
        "a cache key past its size limit is not shared");
}

std::optional<RenderPlan> SingleStackPlan(const Recipe &recipe,
                                          const RecipeGraph &graph) {
  const auto *surface = Get<SurfaceOutput>(recipe.outputs.front());
  const std::array requests{
      RenderStackRequest{&graph, surface, 1, PlacementId{0}, 0, {SideRef{0}}}};
  auto plan = BuildRenderPlan({}, requests, Bindings());
  Check(plan.has_value(), "the synthetic recipe lowers");
  return plan ? std::optional{std::move(*plan)} : std::nullopt;
}

Recipe UniformSourceAndMask() {
  Recipe recipe;
  recipe.masks = {{"uniform", "0.7"}};
  SurfaceOutput surface;
  Layer layer;
  layer.source = Ref{"uniform"};
  layer.mask = Ref{"uniform"};
  surface.stack.push_back(layer);
  recipe.outputs.push_back(surface);
  return recipe;
}

Recipe RippleBeforePositionBake() {
  Recipe recipe;
  TriggerSignal trigger;
  trigger.origin = EventOrigin{"hit"};
  recipe.signals = {{"hit", trigger}};
  RippleSource ripple;
  ripple.trigger = Ref{"hit"};
  recipe.sources = {{"wave", ripple}, {"position", MeshSource{PositionBake{}}}};
  SurfaceOutput surface;
  Layer wave;
  wave.source = Ref{"wave"};
  Layer position;
  position.source = Ref{"position"};
  surface.stack = {wave, position};
  recipe.outputs.push_back(surface);
  return recipe;
}

Recipe ImageOverBake() {
  Recipe recipe;
  ImageSource image;
  image.texture = ImageFile{"shared.dds"};
  recipe.sources = {{"pattern", image},
                    {"position", MeshSource{PositionBake{}}}};
  SurfaceOutput surface;
  Layer pattern;
  pattern.source = Ref{"pattern"};
  Layer position;
  position.source = Ref{"position"};
  surface.stack = {pattern, position};
  recipe.outputs.push_back(surface);
  return recipe;
}

Recipe ImageLayer(const std::optional<CurveRef> &a_curve) {
  Recipe recipe;
  ImageSource image;
  image.texture = ImageFile{"scrolled.dds"};
  image.scroll = Vec2Param{std::array<Param, 2>{0.25f, 0.5f}};
  recipe.sources = {{"pattern", image}};
  SurfaceOutput surface;
  Layer layer;
  layer.source = Ref{"pattern"};
  layer.curve = a_curve;
  surface.stack = {layer};
  recipe.outputs.push_back(surface);
  return recipe;
}

std::vector<const RenderStep *> ImageSamples(const RenderPlan &plan) {
  std::vector<const RenderStep *> samples;
  for (const RenderStep &step : plan.steps)
    if (Is<SampleFieldStep>(step.kind))
      samples.push_back(&step);
  return samples;
}

void ImagesReadOnlyBySamplingAreDirect() {
  for (const auto &curve : {std::optional<CurveRef>{},
                            std::optional{CurveRef{"x / max(mean, 0.01)"}}}) {
    const Recipe recipe = ImageLayer(curve);
    const RecipeGraph graph = RecipeGraph::Compile(recipe);
    const auto plan = SingleStackPlan(recipe, graph);
    if (!plan)
      continue;
    const auto samples = ImageSamples(*plan);
    Check(samples.size() == 2,
          "an image lowers to a scrolled and an unscrolled sample");
    Check(
        std::ranges::all_of(samples,
                            [](const RenderStep *step) {
                              return Get<SampleFieldStep>(step->kind)->direct &&
                                     !step->contentKey;
                            }),
        "image samples read only by programs are direct and unkeyed");
  }
}

std::vector<std::string> DrawnTextureReads(const RenderPlan &plan) {
  std::vector<std::vector<std::string>> unmeasured(plan.steps.size());
  for (const RenderStep &step : plan.steps)
    if (!Is<SubmitReductionStep>(step.kind))
      for (const RenderValueRef ref : InputsOf(step.kind))
        if (const auto *output = Get<StepOutputRef>(ref);
            output && output->step < unmeasured.size())
          unmeasured[output->step].push_back(
              std::string{StepKindName(step.kind)} + " " + step.displayName);
  std::vector<std::string> drawn;
  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    const auto *sample = Get<SampleFieldStep>(plan.steps[i].kind);
    if (!sample || sample->direct || unmeasured[i].empty())
      continue;
    std::string line = plan.steps[i].displayName + " <-";
    for (const std::string &consumer : unmeasured[i])
      line += " [" + consumer + "]";
    drawn.push_back(line);
  }
  return drawn;
}

std::size_t DirectTextureReads(const RenderPlan &plan) {
  return static_cast<std::size_t>(
      std::ranges::count_if(plan.steps, [](const RenderStep &step) {
        const auto *sample = Get<SampleFieldStep>(step.kind);
        return sample && sample->direct;
      }));
}

std::size_t LiveAnimatedPrograms(const RenderPlan &plan) {
  const std::vector<bool> animated = AnimatedSteps(plan);
  const std::vector<std::size_t> consumers = LiveConsumers(plan);
  std::size_t live = 0;
  for (std::size_t s = 0; s < plan.steps.size(); ++s)
    live += animated[s] && consumers[s] > 0 &&
            AsProgramLike(plan, plan.steps[s].kind).has_value();
  return live;
}

void TextureReadsAreSampledDirectly(const std::vector<LoadedRecipe> &recipes) {
  const std::array size{SideRef{0}};
  const auto plan =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings());
  Check(plan.has_value(), "the demo recipes lower together");
  if (!plan)
    return;
  const std::vector<std::string> drawn = DrawnTextureReads(*plan);
  for (const std::string &line : drawn)
    std::printf("drawn texture read: %s\n", line.c_str());
  Check(drawn.empty(),
        "every image and material read in the demo recipes is sampled "
        "directly unless a reduction measures it");
  Check(DirectTextureReads(*plan) > 0, "the recipes read textures");
}

Recipe EmissiveLayers(std::size_t a_layers, const std::string &a_signal) {
  Recipe recipe;
  recipe.signals = {{"t", ExprSignal{a_signal}}};
  recipe.masks = {{"wave", "sin(@t) * 0.5 + 0.5"}, {"flat", "0.25"}};
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  for (std::size_t i = 0; i < a_layers; ++i) {
    Layer layer;
    layer.source = Ref{i % 2 == 0 ? "wave" : "flat"};
    surface.stack.push_back(layer);
  }
  recipe.outputs.push_back(surface);
  return recipe;
}

std::vector<LoadedRecipe> Loaded(std::initializer_list<Recipe> a_recipes) {
  std::vector<LoadedRecipe> loaded;
  for (const Recipe &recipe : a_recipes)
    loaded.push_back({recipe, std::make_shared<const RecipeGraph>(
                                  RecipeGraph::Compile(recipe))});
  return loaded;
}

const LayerField *FieldOf(const CompositeStackStep &stack,
                          const LayerRead &read) {
  const auto *ref = Get<LayerFieldRef>(read);
  return ref && ref->field < stack.fields.size() ? &stack.fields[ref->field]
                                                 : nullptr;
}

bool SameLayerRead(const CompositeStackStep &a, const LayerRead &readA,
                   const CompositeStackStep &b, const LayerRead &readB) {
  const LayerField *fieldA = FieldOf(a, readA);
  const LayerField *fieldB = FieldOf(b, readB);
  if (fieldA || fieldB)
    return fieldA && fieldB && *fieldA == *fieldB;
  return readA == readB;
}

Recipe StaticEmissive() {
  Recipe recipe;
  recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  recipe.masks = {{"dim", "@s * 0.5"}};
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  Layer layer;
  layer.source = Ref{"dim"};
  surface.stack.push_back(layer);
  recipe.outputs.push_back(surface);
  return recipe;
}

void StaticLinksKeepTheirFieldsInAnimatedChains() {
  const std::vector<LoadedRecipe> recipes =
      Loaded({StaticEmissive(), EmissiveLayers(1, "time")});
  const std::array size{SideRef{0}};
  const std::array chains{SlotChainRequest{
      GeometryId{0},
      Surface::kMaterial,
      Slot::kEmissive,
      {ChainLink{PlacementId{0}, 0}, ChainLink{PlacementId{1}, 0}}}};
  const auto plan =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings(), chains);
  Check(plan && plan->mergedChains.size() == 1,
        "a static and an animated link merge");
  if (!plan || plan->mergedChains.size() != 1)
    return;
  const auto &merged = *Get<CompositeStackStep>(
      plan->steps[plan->mergedChains.front().result.step].kind);
  Check(!merged.layers.empty() &&
            Is<RenderValueRef>(merged.layers.front().source),
        "a static link's field stays a cached target in an animated chain");
}

Recipe ImageFields(const std::string &a_prefix,
                   std::initializer_list<int> a_imagesPerLayer) {
  Recipe recipe;
  recipe.signals = {{"t", ExprSignal{"time"}}};
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  int image = 0;
  int field = 0;
  for (const int images : a_imagesPerLayer) {
    std::string product = "sin(@t + " + std::to_string(field) + ")";
    for (int i = 0; i < images; ++i, ++image) {
      ImageSource source;
      source.texture = ImageFile{a_prefix + std::to_string(image) + ".dds"};
      const std::string name = "i" + std::to_string(image);
      recipe.sources.push_back({name, source});
      product += " * @" + name;
    }
    const std::string mask = "m" + std::to_string(field++);
    recipe.masks.push_back({mask, product});
    Layer layer;
    layer.source = Ref{mask};
    surface.stack.push_back(layer);
  }
  recipe.outputs.push_back(surface);
  return recipe;
}

std::size_t MergedPassesOver(const std::vector<LoadedRecipe> &recipes) {
  const std::array size{SideRef{0}};
  const std::array chains{SlotChainRequest{
      GeometryId{0},
      Surface::kMaterial,
      Slot::kEmissive,
      {ChainLink{PlacementId{0}, 0}, ChainLink{PlacementId{1}, 0}}}};
  const auto chained =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings());
  const auto requested =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings(), chains);
  Check(chained && requested, "a chain of image fields lowers");
  if (!chained || !requested)
    return 0;
  return AnimatedPasses(*requested) <= AnimatedPasses(*chained)
             ? requested->mergedChains.size()
             : std::size_t{99};
}

void MergesNeverAddPasses() {
  test::Equal(
      MergedPassesOver(Loaded({ImageFields("a", {5}), ImageFields("b", {5})})),
      std::size_t{1},
      "a merge that leaves one field outside costs no extra pass");
  test::Equal(MergedPassesOver(
                  Loaded({ImageFields("a", {5}), ImageFields("b", {4, 4})})),
              std::size_t{0},
              "a merge that would leave two fields outside is dropped");
}

void SlotChainsMergeIntoOnePass() {
  const std::vector<LoadedRecipe> recipes =
      Loaded({EmissiveLayers(2, "time"), EmissiveLayers(3, "time * 2")});
  const std::array size{SideRef{0}};
  const std::array chains{SlotChainRequest{
      GeometryId{0},
      Surface::kMaterial,
      Slot::kEmissive,
      {ChainLink{PlacementId{0}, 0}, ChainLink{PlacementId{1}, 0}}}};
  const auto chained =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings());
  const auto plan =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings(), chains);
  Check(chained.has_value() && plan.has_value(), "a slot chain plan lowers");
  if (!chained || !plan)
    return;
  Check(ValidateRenderPlan(*plan).has_value(), "a merged plan is valid");
  test::Equal(AnimatedPasses(*plan) + 1, AnimatedPasses(*chained),
              "merging two animated stacks removes one animated pass per tick");
  test::Equal(LiveAnimatedPrograms(*plan), LiveAnimatedPrograms(*chained),
              "merging leaves no animated field outside its stacks");
  test::Equal(plan->mergedChains.size(), std::size_t{1},
              "two stacks on one slot merge into one pass");
  if (plan->mergedChains.size() != 1)
    return;
  const MergedChainBinding &binding = plan->mergedChains.front();
  const auto &merged =
      *Get<CompositeStackStep>(plan->steps[binding.result.step].kind);
  test::Equal(binding.layerStarts, std::vector<std::size_t>{0, 2},
              "each link's layers start where the previous link's end");
  test::Equal(binding.layerCount, std::size_t{5},
              "the merged pass draws every link's layers");
  bool same = merged.layers.size() == 5;
  for (std::size_t link = 0; link < 2 && same; ++link) {
    const auto &original = *Get<CompositeStackStep>(
        plan->steps[plan->stackOutputs[link].result.step].kind);
    if (link == 0)
      Check(merged.base == original.base,
            "the merged pass starts from the first link's base");
    for (std::size_t l = 0; l < original.layers.size() && same; ++l) {
      const PlannedLayer &a = merged.layers[binding.layerStarts[link] + l];
      const PlannedLayer &b = original.layers[l];
      same = SameLayerRead(merged, a.source, original, b.source) &&
             a.opacity == b.opacity && a.blend == b.blend &&
             a.channels == b.channels && a.color == b.color;
    }
  }
  Check(same, "the merged layers are the links' layers in chain order");

  const std::vector<LoadedRecipe> crowded =
      Loaded({EmissiveLayers(5, "time"), EmissiveLayers(4, "time")});
  const auto tooMany =
      BuildRenderPlan({}, StackRequests(crowded, size), Bindings(), chains);
  Check(tooMany && tooMany->mergedChains.empty(),
        "a chain past the one-pass layer limit stays chained");
  const std::array missing{SlotChainRequest{
      GeometryId{0},
      Surface::kMaterial,
      Slot::kEmissive,
      {ChainLink{PlacementId{0}, 0}, ChainLink{PlacementId{7}, 0}}}};
  const auto unmatched =
      BuildRenderPlan({}, StackRequests(recipes, size), Bindings(), missing);
  Check(unmatched && unmatched->mergedChains.empty() &&
            unmatched->inputs.size() == plan->inputs.size() - 1,
        "a chain with a missing link stays chained and adds no input");
}

RenderBindingResolver PerGeometryMesh() {
  return [](const TextureValue &value, GeometryId geometry) {
    return ValueBindings{
        [instance = value.instance, geometry](const ExternalSource &source)
            -> std::expected<std::string, std::string> {
          if (Is<GeometryInput>(source))
            return "mesh " + std::to_string(IndexOf(geometry));
          if (const auto *texture = Get<TextureInput>(source))
            return texture->path;
          return std::to_string(instance);
        },
        [instance = value.instance](NodeId node) {
          return std::to_string(instance) + ":" + std::to_string(node);
        }};
  };
}

std::size_t Report(const char *label, const RenderPlan &plan) {
  const auto duplicates = Duplicates(plan);
  for (const auto &duplicate : duplicates)
    std::printf(
        "%s: step %zu (%s) duplicates step %zu (%s)\n", label, duplicate.second,
        plan.steps[duplicate.second].displayName.c_str(), duplicate.first,
        plan.steps[duplicate.first].displayName.c_str());
  return duplicates.size();
}

Recipe LayerMaskedBy(BakeKind a_bake) {
  Recipe recipe;
  ImageSource image;
  image.texture = ImageFile{"shared.dds"};
  recipe.sources = {{"pattern", image}, {"region", MeshSource{a_bake}}};
  recipe.masks = {{"onRegion", "@region"}};
  SurfaceOutput surface;
  Layer layer;
  layer.source = Ref{"pattern"};
  layer.mask = Ref{"onRegion"};
  surface.stack = {layer};
  recipe.outputs.push_back(surface);
  return recipe;
}

RenderBindingResolver SharedMeshNamed(std::vector<std::string> a_names) {
  return [names = std::move(a_names)](const TextureValue &value,
                                      GeometryId geometry) {
    ValueBindings bindings{
        [instance = value.instance](const ExternalSource &source)
            -> std::expected<std::string, std::string> {
          if (Is<GeometryInput>(source))
            return std::string{"one mesh"};
          if (const auto *texture = Get<TextureInput>(source))
            return texture->path;
          return std::to_string(instance);
        },
        [instance = value.instance](NodeId node) {
          return std::to_string(instance) + ":" + std::to_string(node);
        }};
    const std::size_t index = IndexOf(geometry);
    bindings.geometry = index < names.size() ? names[index] : std::string{};
    return bindings;
  };
}

struct BakeCounts {
  std::size_t bakes = 0;
  std::size_t constants = 0;
};

std::optional<BakeCounts> LowerOntoNamed(const Recipe &a_recipe,
                                         std::vector<std::string> a_names) {
  const RecipeGraph graph = RecipeGraph::Compile(a_recipe);
  const auto *surface = Get<SurfaceOutput>(a_recipe.outputs.front());
  std::vector<RenderStackRequest> requests;
  for (std::size_t g = 0; g < a_names.size(); ++g)
    requests.push_back(
        {&graph, surface, 1, PlacementId{0}, 0, {SideRef{0}}, GeometryId{g}});
  const auto plan =
      BuildRenderPlan({}, requests, SharedMeshNamed(std::move(a_names)));
  if (!plan)
    return std::nullopt;
  BakeCounts counts;
  for (const auto &step : plan->steps) {
    counts.bakes += Is<BakeMeshStep>(step.kind);
    const auto *constant = Get<ConstantRenderStep>(step.kind);
    counts.constants += constant && *constant == ConstantRenderStep{0.0f};
  }
  return counts;
}

void GeometryBakesLowerByName() {
  const Recipe guarded = LayerMaskedBy(GeometryBake{"Cuirass*"});
  const auto matched = LowerOntoNamed(guarded, {"Cuirass_1"});
  Check(matched && matched->bakes == 1 && matched->constants == 0,
        "a geometry bake on a named geometry bakes the mesh coverage");
  const auto elsewhere = LowerOntoNamed(guarded, {"Belt"});
  Check(elsewhere && elsewhere->bakes == 0 && elsewhere->constants == 1,
        "a geometry bake on another geometry lowers to a constant 0");
  const auto mixed = LowerOntoNamed(guarded, {"Cuirass_1", "Belt"});
  Check(mixed && mixed->bakes == 1 && mixed->constants == 1,
        "one mesh under two names keys the bake apart by the name's match");
  const auto both = LowerOntoNamed(guarded, {"Cuirass_1", "Cuirass_2"});
  Check(both && both->bakes == 1,
        "two names that both match share one coverage bake of one mesh");
  const auto unguarded =
      LowerOntoNamed(LayerMaskedBy(PartitionBake{}), {"Cuirass_1", "Belt"});
  Check(unguarded && unguarded->bakes == 1 && unguarded->constants == 0,
        "without a geometry bake the geometry name keys nothing apart");
}
}

int main() {
  std::vector<LoadedRecipe> recipes;
  const auto folder =
      test::Fixtures().parent_path().parent_path() / "recipes/examples";
  for (const std::string name :
       {"arcane-circuit", "resonant-ward", "winterglass"}) {
    auto loaded = ParseRecipe(test::ReadFile(folder / (name + ".json")), name);
    Check(loaded.recipe && !loaded.HasErrors(), "demo recipe loads cleanly");
    if (!loaded.recipe || loaded.HasErrors())
      return test::Finish("render plan sharing");
    auto graph = std::make_shared<const RecipeGraph>(
        RecipeGraph::Compile(*loaded.recipe));
    recipes.push_back({*loaded.recipe, std::move(graph)});
  }
  GeometryBakesLowerByName();
  ContentKeysMarkOnlyStaticTextureSteps(recipes);
  TextureReadsAreSampledDirectly(recipes);
  ImagesReadOnlyBySamplingAreDirect();
  SlotChainsMergeIntoOnePass();
  MergesNeverAddPasses();
  StaticLinksKeepTheirFieldsInAnimatedChains();
  CacheKeysCarryOperandValues();
  RandMasksAreStatic();
  const std::array oneSize{SideRef{0}};
  const auto single =
      BuildRenderPlan({}, StackRequests(recipes, oneSize), Bindings());
  Check(single.has_value(), "the demo recipes lower into one geometry plan");
  const std::array twoSizes{SideRef{0}, SideRef{1}};
  const auto mixed =
      BuildRenderPlan({}, StackRequests(recipes, twoSizes), Bindings());
  Check(mixed.has_value(), "the demo recipes lower at two stack sizes");
  if (!single || !mixed)
    return test::Finish("render plan sharing");
  bool mipsOnlyForStacks = true;
  for (const auto &step : single->steps)
    if (const auto requirements = RequirementsOf(step.kind))
      mipsOnlyForStacks = mipsOnlyForStacks &&
                          (requirements->mipPolicy == MipPolicy::kGenerate) ==
                              Is<CompositeStackStep>(step.kind);
  Check(mipsOnlyForStacks, "only stack results generate mips; every "
                           "intermediate is read at its own size");
  test::Equal(Report("one size", *single), std::size_t{0},
              "complete sharing: no two steps have equal kinds at one size");
  test::Equal(Report("two sizes", *mixed), std::size_t{0},
              "complete sharing: no two steps have equal kinds at two sizes");
  const auto uniform = UniformSourceAndMask();
  const auto uniformGraph = RecipeGraph::Compile(uniform);
  if (const auto plan = SingleStackPlan(uniform, uniformGraph))
    test::Equal(Report("uniform source and mask", *plan), std::size_t{0},
                "complete sharing: a uniform used as source and mask gets one "
                "fill step");
  const auto rippled = RippleBeforePositionBake();
  const auto rippledGraph = RecipeGraph::Compile(rippled);
  if (const auto plan = SingleStackPlan(rippled, rippledGraph))
    test::Equal(Report("ripple before position bake", *plan), std::size_t{0},
                "complete sharing: a position bake lowered after a ripple "
                "shares the ripple's bake");
  const auto layered = ImageOverBake();
  const auto layeredGraph = RecipeGraph::Compile(layered);
  const auto *layeredSurface = Get<SurfaceOutput>(layered.outputs.front());
  const auto lowerOnto = [&](std::size_t geometries) {
    std::vector<RenderStackRequest> requests;
    for (std::size_t g = 0; g < geometries; ++g)
      requests.push_back({&layeredGraph,
                          layeredSurface,
                          1,
                          PlacementId{0},
                          0,
                          {SideRef{0}},
                          GeometryId{g}});
    return BuildRenderPlan({}, requests, PerGeometryMesh());
  };
  struct Counts {
    std::size_t samples = 0, programs = 0, submissions = 0, bakes = 0,
                stacks = 0, meshes = 0, images = 0;
  };
  const auto count = [](const RenderPlan &plan) {
    Counts counts;
    for (const auto &step : plan.steps) {
      counts.samples += Is<SampleFieldStep>(step.kind);
      counts.programs += Is<EvaluateProgramStep>(step.kind);
      counts.submissions += Is<SubmitReductionStep>(step.kind);
      counts.bakes += Is<BakeMeshStep>(step.kind);
      counts.stacks += Is<CompositeStackStep>(step.kind);
    }
    for (const auto &input : plan.inputs) {
      counts.meshes += input.type == RenderValueType{RenderResourceType::kMesh};
      counts.images +=
          input.type == RenderValueType{RenderResourceType::kTexture};
    }
    return counts;
  };
  const auto one = lowerOnto(1);
  const auto two = lowerOnto(2);
  Check(one && two, "one plan lowers a recipe onto one and two geometries");
  if (one && two) {
    const auto single = count(*one), pair = count(*two);
    test::Equal(pair.samples, single.samples,
                "image samples do not grow with the geometry count");
    test::Equal(pair.programs, single.programs,
                "image programs do not grow with the geometry count");
    test::Equal(pair.submissions, single.submissions,
                "image measurements do not grow with the geometry count");
    test::Equal(pair.images, single.images,
                "both geometries share the imported image");
    test::Equal(pair.meshes, 2 * single.meshes,
                "each geometry imports its own mesh");
    test::Equal(pair.bakes, 2 * single.bakes,
                "each geometry bakes its own mesh");
    test::Equal(pair.stacks, 2 * single.stacks,
                "each geometry composites its own stack");
    test::Equal(Report("two geometries", *two), std::size_t{0},
                "complete sharing holds across geometries");
    const auto boundFor = [&](GeometryId geometry) {
      std::vector<std::pair<NodeId, std::size_t>> bound;
      for (const auto &binding : two->values)
        if (binding.geometry == geometry)
          bound.emplace_back(binding.value.output.node,
                             binding.value.output.output);
      std::ranges::sort(bound);
      bound.erase(std::unique(bound.begin(), bound.end()), bound.end());
      return bound;
    };
    Check(!boundFor(GeometryId{0}).empty() &&
              boundFor(GeometryId{0}) == boundFor(GeometryId{1}),
          "every value bound for one geometry is also bound for the other, so "
          "inspection finds shared intermediates on both");
  }
  return test::Finish("render plan sharing");
}
