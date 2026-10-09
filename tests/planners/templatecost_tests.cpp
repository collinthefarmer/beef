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
constexpr std::uint64_t kFillTemplateHeldBytes = std::uint64_t{58} << 20;

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
  PlanSizes sizes;
};

SideRef SideFor(std::vector<TextureSize> &a_sizes, TextureSize a_size) {
  const auto found = std::ranges::find(a_sizes, a_size);
  if (found != a_sizes.end()) {
    return {static_cast<std::uint32_t>(found - a_sizes.begin())};
  }
  a_sizes.push_back(a_size);
  return {static_cast<std::uint32_t>(a_sizes.size() - 1)};
}

std::optional<SizedPlan> PlanFor(const Recipe &a_recipe,
                                 const RecipeGraph &a_graph,
                                 const RenderBindingResolver &a_wearer,
                                 std::uint32_t a_base) {
  std::vector<RenderStackRequest> requests;
  std::vector<TextureSize> sizes;
  for (std::size_t o = 0; o < a_recipe.outputs.size(); ++o) {
    if (const auto *surface = Get<SurfaceOutput>(a_recipe.outputs[o])) {
      requests.push_back({&a_graph,
                          surface,
                          1,
                          PlacementId{o},
                          o,
                          {SideFor(sizes, StackSize(*surface, a_base))}});
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
  std::expected<PlanSizes, std::string> resolved =
      SizesFor(*plan, std::move(sizes));
  Check(resolved.has_value(), "the plan names only the sizes it was given");
  if (!resolved) {
    return std::nullopt;
  }
  return SizedPlan{std::move(*plan), std::move(*resolved)};
}

std::string Describe(const TargetCost &a_cost) {
  std::string sizes;
  for (const auto &[size, count] : a_cost.targetsBySize) {
    sizes += std::format(" {}x{}", count, size);
  }
  return std::format("{} targets, {:.1f} MiB ({} )", a_cost.targets,
                     static_cast<double>(a_cost.bytes) / (1024.0 * 1024.0),
                     sizes);
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
  const TextureSize size{512};
  const HeldTarget keyed{size, TextureFormat::kRgba8, "key"};
  const HeldTarget unkeyed{size, TextureFormat::kRgba8, std::nullopt};
  const std::vector<HeldTarget> targets{keyed, keyed, unkeyed, unkeyed};
  const TargetCost cost = CostOf(targets);
  const std::uint64_t one =
      TargetBytes(size, TextureFormat::kRgba8, MipPolicy::kGenerate);
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
      TargetsOf(a_wearers.first.plan, a_wearers.first.sizes);
  const PlanTargets same =
      TargetsOf(a_wearers.sameItem.plan, a_wearers.sameItem.sizes);
  const PlanTargets other =
      TargetsOf(a_wearers.otherItem.plan, a_wearers.otherItem.sizes);
  const TargetCost held = CostOf(first.withIntermediates);
  const TargetCost stacks = CostOf(first.stacks);
  const TargetCost twoSame =
      CostOf(Joined(first.withIntermediates, same.withIntermediates));
  const TargetCost twoOther =
      CostOf(Joined(first.withIntermediates, other.withIntermediates));
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

std::string_view TypeName(ValueType a_type) {
  switch (a_type) {
  case ValueType::kScalar:
    return "scalar";
  case ValueType::kVec2:
    return "vec2";
  case ValueType::kVec3:
    return "vec3";
  }
  return "scalar";
}

std::string ValueTypesOf(const RenderPlan &a_plan, RenderStepId a_step) {
  if (const auto *stack = Get<CompositeStackStep>(a_plan.steps[a_step].kind)) {
    return std::format("{} slot", SlotName(stack->slot));
  }
  std::string types;
  for (const RenderValueBinding &binding : a_plan.values) {
    const auto *result = Get<StepOutputRef>(binding.result);
    if (!result || result->step != a_step || !binding.value.graph) {
      continue;
    }
    const std::optional<GraphValueType> type =
        binding.value.graph->OutputType(binding.value.output);
    const auto *value = type ? Get<ValueType>(*type) : nullptr;
    const std::string_view name = value ? TypeName(*value) : "resource";
    if (types.find(name) == std::string::npos) {
      types += types.empty() ? std::string{name} : std::format("/{}", name);
    }
  }
  return types.empty() ? std::string{"untyped"} : types;
}

std::vector<RenderStepId> ReadersOf(const RenderPlan &a_plan,
                                    RenderStepId a_step) {
  std::vector<RenderStepId> readers;
  for (RenderStepId reader = 0; reader < a_plan.steps.size(); ++reader) {
    for (const RenderValueRef operand : StepDependencies(a_plan, reader)) {
      const auto *output = Get<StepOutputRef>(operand);
      if (output && output->step == a_step &&
          !std::ranges::contains(readers, reader)) {
        readers.push_back(reader);
      }
    }
  }
  return readers;
}

std::string ConsumersOf(const RenderPlan &a_plan, RenderStepId a_step) {
  if (Is<CompositeStackStep>(a_plan.steps[a_step].kind)) {
    return "game material";
  }
  const std::optional<TextureRequirements> own =
      RequirementsOf(a_plan.steps[a_step].kind);
  std::string consumers;
  for (const RenderStepId reader : ReadersOf(a_plan, a_step)) {
    const std::optional<TextureRequirements> theirs =
        RequirementsOf(a_plan.steps[reader].kind);
    const bool otherSide = own && theirs && own->side != theirs->side;
    consumers += std::format("{}{}#{}{}", consumers.empty() ? "" : ", ",
                             StepKindName(a_plan.steps[reader].kind), reader,
                             otherSide ? " (other side)" : "");
  }
  return consumers;
}

void ListHeldTargets(const SizedPlan &a_sized, std::uint32_t a_base) {
  const RenderPlan &plan = a_sized.plan;
  const std::vector<bool> animated = AnimatedSteps(plan);
  std::cout << std::format("held targets at base {}\n", a_base);
  for (const RenderStepId step : HeldSteps(plan)) {
    const RenderStep &planned = plan.steps[step];
    const std::optional<TextureRequirements> requirements =
        RequirementsOf(planned.kind);
    if (!requirements) {
      continue;
    }
    const TextureSize size = SizeOf(a_sized.sizes, requirements->side);
    std::cout << std::format(
        "  #{} {} '{}' ({}): {} {} {} mips, {:.2f} MiB; {}; read by {}\n", step,
        StepKindName(planned.kind), planned.displayName,
        animated[step] ? "animated" : "static", size.Pixels(),
        requirements->format == TextureFormat::kRgba32Float ? "rgba32f"
                                                            : "rgba8",
        requirements->mipPolicy == MipPolicy::kGenerate ? "with" : "no",
        static_cast<double>(
            TargetBytes(size, requirements->format, requirements->mipPolicy)) /
            (1024.0 * 1024.0),
        ValueTypesOf(plan, step), ConsumersOf(plan, step));
  }
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
    const PlanSizes leveled = AtLevel(full->sizes, level);
    Check(
        SameStructure(full->plan, fresh->plan) && leveled == fresh->sizes &&
            CostOf(TargetsOf(full->plan, leveled).withIntermediates) ==
                CostOf(TargetsOf(fresh->plan, fresh->sizes).withIntermediates),
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
      ListHeldTargets(wearers->first, base);
    }
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
