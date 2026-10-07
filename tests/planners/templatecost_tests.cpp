// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"
#include "planners/TargetCost.h"
#include "recipe/Importer.h"
#include "recipe/RecipeGraph.h"
#include "test_support.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::uint32_t kBaseSide = 2048;
constexpr std::uint64_t kFillTemplateTargetBytes = std::uint64_t{88} << 20;

bool ActorScoped(const ExternalSource &a_source) {
  return Is<EffectShaderInput>(a_source) || Is<EventInput>(a_source) ||
         Is<TimeInput>(a_source) || Is<DeltaTimeInput>(a_source) ||
         Is<RootTransformInput>(a_source) || Is<ActorValueSignal>(a_source) ||
         Is<ActorStateSignal>(a_source) || Is<EnchantmentSignal>(a_source);
}

RenderBindingResolver Wearer(std::string a_actor, std::string a_item) {
  return [actor = std::move(a_actor),
          item = std::move(a_item)](const TextureValue &a_value, GeometryId) {
    const std::string instance = std::format("{}:{}", actor, a_value.instance);
    return ValueBindings{[instance, item](const ExternalSource &a_source)
                             -> std::expected<std::string, std::string> {
                           if (Is<ItemHashInput>(a_source)) {
                             return "item:" + item;
                           }
                           return ActorScoped(a_source) ? instance
                                                        : std::string{"shared"};
                         },
                         [instance](NodeId a_node) {
                           return std::format("{}:{}", instance, a_node);
                         }};
  };
}

TextureSize StackSize(const SurfaceOutput &a_surface) {
  const Resolution resolution =
      a_surface.resolution.value_or(DefaultSlotResolution(a_surface.slot));
  return TextureSize{kBaseSide / ResolutionDivisor(resolution)};
}

std::optional<Recipe> ReadTemplate(const std::filesystem::path &a_path) {
  const LoadResult loaded = ParseRecipe(test::ReadFile(a_path), "fill");
  Check(loaded.recipe.has_value() && !loaded.HasErrors(),
        std::format("{} parses without errors", a_path.string()));
  return loaded.recipe;
}

std::optional<Recipe> ImportedRecipe(const Recipe &a_template) {
  const auto record = ParseEffectShaderRecord(
      test::ReadFile(test::Fixtures() / "efsh" / "SyntheticTiled.json"));
  Check(record.has_value(), "the SyntheticTiled record parses");
  return record ? std::optional{ImportEffectShader(*record, a_template)}
                : std::nullopt;
}

std::optional<RenderPlan> PlanFor(const Recipe &a_recipe,
                                  const RecipeGraph &a_graph,
                                  const RenderBindingResolver &a_wearer) {
  std::vector<RenderStackRequest> requests;
  for (std::size_t o = 0; o < a_recipe.outputs.size(); ++o) {
    if (const auto *surface = Get<SurfaceOutput>(a_recipe.outputs[o])) {
      requests.push_back(
          {&a_graph, surface, 1, PlacementId{o}, o, {StackSize(*surface)}});
    }
  }
  std::expected<RenderPlan, std::string> plan =
      BuildRenderPlan({}, requests, a_wearer);
  if (!plan) {
    std::cerr << plan.error() << '\n';
  }
  Check(plan.has_value(),
        "the imported template lowers and its field programs pack");
  return plan ? std::optional{std::move(*plan)} : std::nullopt;
}

std::string Describe(const TargetCost &a_cost) {
  std::string sides;
  for (const auto &[side, count] : a_cost.targetsBySide) {
    sides += std::format(" {}x{}", count, side);
  }
  return std::format("{} targets, {:.1f} MiB ({} )", a_cost.targets,
                     static_cast<double>(a_cost.bytes) / (1024.0 * 1024.0),
                     sides);
}
}

int main(int argc, char **argv) {
  const std::filesystem::path path = argc > 1 ? std::filesystem::path{argv[1]}
                                              : test::Templates() / "fill.json";
  const std::optional<Recipe> fill = ReadTemplate(path);
  const std::optional<Recipe> recipe =
      fill ? ImportedRecipe(*fill) : std::nullopt;
  if (!recipe) {
    return test::Finish("fill template cost");
  }
  const RecipeGraph graph = RecipeGraph::Compile(*recipe);
  const std::optional<RenderPlan> first =
      PlanFor(*recipe, graph, Wearer("actor-a", "1"));
  const std::optional<RenderPlan> sameItem =
      PlanFor(*recipe, graph, Wearer("actor-b", "1"));
  const std::optional<RenderPlan> otherItem =
      PlanFor(*recipe, graph, Wearer("actor-b", "2"));
  if (!first || !sameItem || !otherItem) {
    return test::Finish("fill template cost");
  }
  const TargetCost cost = TargetCostOf(*first);
  const TargetCost sharedSame = SharedTargetCostOf(*first, *sameItem);
  const TargetCost sharedOther = SharedTargetCostOf(*first, *otherItem);
  std::cout << std::format("template {}\n  steps {}\n  per wearer: {}\n"
                           "  shared, same item: {}\n"
                           "  shared, other item: {}\n",
                           path.string(), first->steps.size(), Describe(cost),
                           Describe(sharedSame), Describe(sharedOther));
  Check(cost.bytes <= kFillTemplateTargetBytes,
        std::format("the fill template stays within {} target bytes",
                    kFillTemplateTargetBytes));
  Check(sharedOther.bytes <= sharedSame.bytes,
        "a second item never shares more than a copy of the same item");
  return test::Finish("fill template cost");
}
