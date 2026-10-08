// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"
#include "planners/ValueIdentity.h"
#include "recipe/Importer.h"
#include "recipe/RecipeGraph.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::uint32_t kGuardedBaseSide = 2048;
constexpr std::array<std::uint32_t, 3> kBaseSides{1024, 2048, 4096};
constexpr std::uint64_t kFillTemplateHeldBytes = std::uint64_t{76} << 20;

bool ActorScoped(const ExternalSource &a_source) {
  return Is<EffectShaderInput>(a_source) || Is<EventInput>(a_source) ||
         Is<TimeInput>(a_source) || Is<DeltaTimeInput>(a_source) ||
         Is<RootTransformInput>(a_source) || Is<ActorValueSignal>(a_source) ||
         Is<ActorStateSignal>(a_source);
}

constexpr std::array<float, kEnchantmentFieldCount> kEnchantment{25.0f, 0.0f,
                                                                 0.5f, 0.25f};

RenderBindingResolver Wearer(std::string a_actor, std::string a_item) {
  return [actor = std::move(a_actor),
          item = std::move(a_item)](const TextureValue &a_value, GeometryId) {
    const std::string instance = std::format("{}:{}", actor, a_value.instance);
    return ValueBindings{
        [instance, item](const ExternalSource &a_source)
            -> std::expected<std::string, std::string> {
          if (Is<ItemHashInput>(a_source)) {
            return "item:" + item;
          }
          if (const auto *enchantment = Get<EnchantmentSignal>(a_source)) {
            return EnchantmentIdentity(*enchantment, kEnchantment);
          }
          return ActorScoped(a_source) ? instance : std::string{"shared"};
        },
        [instance](NodeId a_node) {
          return std::format("{}:{}", instance, a_node);
        }};
  };
}

TextureSize StackSize(const SurfaceOutput &a_surface, std::uint32_t a_base) {
  const Resolution resolution =
      a_surface.resolution.value_or(DefaultSlotResolution(a_surface.slot));
  return TextureSize{a_base / ResolutionDivisor(resolution)};
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

struct SizedPlan {
  RenderPlan plan;
  PlanSides sides;
};

SideRef SideFor(std::vector<TextureSize> &a_sides, TextureSize a_size) {
  const auto found = std::ranges::find(a_sides, a_size);
  if (found != a_sides.end()) {
    return {static_cast<std::uint32_t>(found - a_sides.begin())};
  }
  a_sides.push_back(a_size);
  return {static_cast<std::uint32_t>(a_sides.size() - 1)};
}

std::optional<SizedPlan> PlanFor(const Recipe &a_recipe,
                                 const RecipeGraph &a_graph,
                                 const RenderBindingResolver &a_wearer,
                                 std::uint32_t a_base) {
  std::vector<RenderStackRequest> requests;
  std::vector<TextureSize> sides;
  for (std::size_t o = 0; o < a_recipe.outputs.size(); ++o) {
    if (const auto *surface = Get<SurfaceOutput>(a_recipe.outputs[o])) {
      requests.push_back({&a_graph,
                          surface,
                          1,
                          PlacementId{o},
                          o,
                          {SideFor(sides, StackSize(*surface, a_base))}});
    }
  }
  std::expected<RenderPlan, std::string> plan =
      BuildRenderPlan({}, requests, a_wearer);
  if (!plan) {
    std::cerr << plan.error() << '\n';
  }
  Check(plan.has_value(),
        "the imported template lowers and its field programs pack");
  if (!plan) {
    return std::nullopt;
  }
  std::expected<PlanSides, std::string> resolved =
      SidesFor(*plan, std::move(sides));
  Check(resolved.has_value(), "the plan names only the sides it was given");
  if (!resolved) {
    return std::nullopt;
  }
  return SizedPlan{std::move(*plan), std::move(*resolved)};
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

std::vector<HeldTarget> Joined(std::span<const HeldTarget> a_first,
                               std::span<const HeldTarget> a_second) {
  std::vector<HeldTarget> joined{a_first.begin(), a_first.end()};
  joined.insert(joined.end(), a_second.begin(), a_second.end());
  return joined;
}

std::size_t StacksIn(const RenderPlan &a_plan) {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_plan.steps, [](const RenderStep &a_step) {
        return Is<CompositeStackStep>(a_step.kind);
      }));
}

void OneTargetPerContentKey() {
  const TextureSize side{512};
  const HeldTarget keyed{side, TextureFormat::kRgba8, "key"};
  const HeldTarget unkeyed{side, TextureFormat::kRgba8, std::nullopt};
  const std::vector<HeldTarget> targets{keyed, keyed, unkeyed, unkeyed};
  const TargetCost cost = CostOf(targets);
  const std::uint64_t one = TargetBytes(side, TextureFormat::kRgba8);
  Check(cost.targets == 3 && cost.bytes == 3 * one &&
            one == std::uint64_t{512} * 512 * 4 * 4 / 3,
        "a content key counts once and an unkeyed target counts each time");
}

struct Wearers {
  SizedPlan first, sameItem, otherItem;
};

std::optional<Wearers> WearersAt(const Recipe &a_recipe,
                                 const RecipeGraph &a_graph,
                                 std::uint32_t a_base) {
  std::optional<SizedPlan> first =
      PlanFor(a_recipe, a_graph, Wearer("actor-a", "1"), a_base);
  std::optional<SizedPlan> sameItem =
      PlanFor(a_recipe, a_graph, Wearer("actor-b", "1"), a_base);
  std::optional<SizedPlan> otherItem =
      PlanFor(a_recipe, a_graph, Wearer("actor-b", "2"), a_base);
  if (!first || !sameItem || !otherItem) {
    return std::nullopt;
  }
  return Wearers{std::move(*first), std::move(*sameItem),
                 std::move(*otherItem)};
}

TargetCost CheckedCost(const Wearers &a_wearers, std::uint32_t a_base) {
  const PlanTargets first =
      TargetsOf(a_wearers.first.plan, a_wearers.first.sides);
  const PlanTargets same =
      TargetsOf(a_wearers.sameItem.plan, a_wearers.sameItem.sides);
  const PlanTargets other =
      TargetsOf(a_wearers.otherItem.plan, a_wearers.otherItem.sides);
  const TargetCost held = CostOf(first.held);
  const TargetCost stacks = CostOf(first.stacks);
  const TargetCost twoSame = CostOf(Joined(first.held, same.held));
  const TargetCost twoOther = CostOf(Joined(first.held, other.held));
  std::cout << std::format("base {}\n  held: {}\n  stacks: {}\n"
                           "  held, two wearers of one item: {}\n"
                           "  held, two wearers of two items: {}\n",
                           a_base, Describe(held), Describe(stacks),
                           Describe(twoSame), Describe(twoOther));
  Check(stacks.targets == StacksIn(a_wearers.first.plan) &&
            stacks.bytes <= held.bytes,
        std::format("at base {} a plan holds its stacks among its targets",
                    a_base));
  Check(twoSame.bytes <= twoOther.bytes && twoOther.bytes <= 2 * held.bytes,
        std::format("at base {} a second wearer of one item shares at least "
                    "as much as a wearer of another item",
                    a_base));
  return held;
}

void LevelsMatchFreshPlans(const Recipe &a_recipe, const RecipeGraph &a_graph) {
  const RenderBindingResolver wearer = Wearer("actor-a", "1");
  for (const auto &[base, level, smaller] :
       {std::tuple{2048u, ResolutionLevel::kHalf, 1024u},
        std::tuple{4096u, ResolutionLevel::kEighth, 512u}}) {
    const std::optional<SizedPlan> full =
        PlanFor(a_recipe, a_graph, wearer, base);
    const std::optional<SizedPlan> fresh =
        PlanFor(a_recipe, a_graph, wearer, smaller);
    if (!full || !fresh) {
      return;
    }
    const PlanSides leveled = AtLevel(full->sides, level);
    Check(SameStructure(full->plan, fresh->plan) && leveled == fresh->sides &&
              CostOf(TargetsOf(full->plan, leveled).held) ==
                  CostOf(TargetsOf(fresh->plan, fresh->sides).held),
          std::format("a base {} plan at level {} holds what a fresh base {} "
                      "plan holds",
                      base, LevelName(level), smaller));
  }
}

int main(int argc, char **argv) {
  OneTargetPerContentKey();
  const std::filesystem::path path = argc > 1 ? std::filesystem::path{argv[1]}
                                              : test::Templates() / "fill.json";
  const std::optional<Recipe> fill = ReadTemplate(path);
  const std::optional<Recipe> recipe =
      fill ? ImportedRecipe(*fill) : std::nullopt;
  if (!recipe) {
    return test::Finish("fill template cost");
  }
  const RecipeGraph graph = RecipeGraph::Compile(*recipe);
  std::cout << std::format("template {}\n", path.string());
  for (const std::uint32_t base : kBaseSides) {
    const std::optional<Wearers> wearers = WearersAt(*recipe, graph, base);
    if (!wearers) {
      return test::Finish("fill template cost");
    }
    const TargetCost held = CheckedCost(*wearers, base);
    if (base == kGuardedBaseSide) {
      Check(held.bytes <= kFillTemplateHeldBytes,
            std::format("the fill template holds at most {} target bytes at "
                        "base {}",
                        kFillTemplateHeldBytes, base));
    }
  }
  LevelsMatchFreshPlans(*recipe, graph);
  return test::Finish("fill template cost");
}
