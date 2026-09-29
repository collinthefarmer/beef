// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"
#include "test_support.h"

#include <algorithm>
#include <cstdio>
#include <memory>
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
              std::span<const TextureSize> sizes) {
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

std::optional<RenderPlan> SingleStackPlan(const Recipe &recipe,
                                          const RecipeGraph &graph) {
  const auto *surface = Get<SurfaceOutput>(recipe.outputs.front());
  const std::array requests{RenderStackRequest{
      &graph, surface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
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
  recipe.sources = {{"wave", ripple}, {"position", BakeSource{PositionBake{}}}};
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
  image.path = "shared.dds";
  recipe.sources = {{"pattern", image},
                    {"position", BakeSource{PositionBake{}}}};
  SurfaceOutput surface;
  Layer pattern;
  pattern.source = Ref{"pattern"};
  Layer position;
  position.source = Ref{"position"};
  surface.stack = {pattern, position};
  recipe.outputs.push_back(surface);
  return recipe;
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
  const std::array oneSize{TextureSize{512}};
  const auto single =
      BuildRenderPlan({}, StackRequests(recipes, oneSize), Bindings());
  Check(single.has_value(), "the demo recipes lower into one geometry plan");
  const std::array twoSizes{TextureSize{512}, TextureSize{1024}};
  const auto mixed =
      BuildRenderPlan({}, StackRequests(recipes, twoSizes), Bindings());
  Check(mixed.has_value(), "the demo recipes lower at two stack sizes");
  if (!single || !mixed)
    return test::Finish("render plan sharing");
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
                          {TextureSize{64}},
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
