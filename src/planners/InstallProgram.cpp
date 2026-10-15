// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InstallProgram.h"

#include "Core.h"
#include "planners/TargetCost.h"

#include <algorithm>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
const Recipe *StoredRecipe(std::span<const Recipe> a_store,
                           RecipeId a_recipe) noexcept {
  const std::size_t index = IndexOf(a_recipe);
  return index < a_store.size() ? &a_store[index] : nullptr;
}

const Recipe *RecipeOfInstance(const ActorMatches &a_matches,
                               std::span<const Recipe> a_store,
                               InstanceId a_instance) noexcept {
  const Instance *instance = InstanceAt(a_matches, a_instance);
  return instance ? StoredRecipe(a_store, instance->recipe) : nullptr;
}

std::optional<InstanceId>
InstanceOfContribution(const ActorMatches &a_matches,
                       const GeometryPlacementPlan &a_placement,
                       SlotContributor a_placed) {
  return InstanceOfPlaced(a_matches, a_placement.sources, IndexOf(a_placed));
}

std::optional<PlacementId>
PlacementOfContribution(const GeometryPlacementPlan &a_placement,
                        SlotContributor a_placed) noexcept {
  const std::size_t index = IndexOf(a_placed);
  if (index >= a_placement.sources.size()) {
    return std::nullopt;
  }
  return a_placement.sources[index];
}

PlacementOutputs OutputsOfPlacement(const Placement &a_placement,
                                    PlacementId a_id, GeometryId a_geometry) {
  PlacementOutputs outputs{a_id, a_geometry, {}};
  for (const OutputPlacement &output : a_placement.outputs) {
    outputs.outputs.push_back(
        {IndexOf(output.output), OutputInactive{output.problem}});
  }
  return outputs;
}

PlacedOutputState *OutputStateAt(std::vector<PlacementOutputs> &a_placements,
                                 PlacementId a_placement,
                                 std::size_t a_output) noexcept {
  const auto placement = std::ranges::find(a_placements, a_placement,
                                           &PlacementOutputs::placement);
  if (placement == a_placements.end()) {
    return nullptr;
  }
  const auto output = std::ranges::find(placement->outputs, a_output,
                                        &PlacedOutputState::output);
  return output == placement->outputs.end() ? nullptr : &*output;
}

std::string ReplacerRecipeId(const ActorMatches &a_matches,
                             std::span<const Recipe> a_store,
                             const GeometryPlacementPlan &a_placement,
                             const SlotPlan &a_slot,
                             const SlotContribution &a_replaced) {
  const std::optional<std::size_t> replacer = ReplacerOf(a_slot, a_replaced);
  if (!replacer) {
    return {};
  }
  const std::optional<InstanceId> instance =
      InstanceOfPlaced(a_matches, a_placement.sources, *replacer);
  const Recipe *recipe =
      instance ? RecipeOfInstance(a_matches, a_store, *instance) : nullptr;
  return recipe ? recipe->id : std::string{};
}

void MarkReplaced(std::vector<PlacementOutputs> &a_placements,
                  const GeometryPlacementPlan &a_placement,
                  const ActorMatches &a_matches,
                  std::span<const Recipe> a_store) {
  for (const SlotPlan &slot : a_placement.plan.slots) {
    for (const SlotContribution &replaced : slot.replaced) {
      const std::optional<PlacementId> placement =
          PlacementOfContribution(a_placement, replaced.placed);
      PlacedOutputState *output =
          placement ? OutputStateAt(a_placements, *placement, replaced.output)
                    : nullptr;
      if (!output) {
        continue;
      }
      output->state = WithProblem(
          output->state,
          std::format("replaced by recipe {}",
                      ReplacerRecipeId(a_matches, a_store, a_placement, slot,
                                       replaced)));
    }
  }
}

std::optional<InstanceId> ShellOwnerOf(const ActorMatches &a_matches,
                                       std::span<const Recipe> a_store,
                                       const GeometryBinding &a_geometry) {
  if (!a_geometry.binding.shell || !a_geometry.binding.shellOwner) {
    return std::nullopt;
  }
  const std::optional<InstanceId> owner = InstanceOfContribution(
      a_matches, a_geometry.placement, *a_geometry.binding.shellOwner);
  if (!owner || !RecipeOfInstance(a_matches, a_store, *owner)) {
    return std::nullopt;
  }
  return owner;
}

const BoundSurfaces *BoundAt(std::span<const BoundSurfaces> a_bound,
                             GeometryId a_geometry) noexcept {
  const auto found =
      std::ranges::find(a_bound, a_geometry, &BoundSurfaces::geometry);
  return found == a_bound.end() ? nullptr : &*found;
}

OutputState TargetState(const SlotTargetState &a_target) {
  if (a_target.problem.empty()) {
    return OutputPending{};
  }
  if (a_target.refusal == SlotRefusal::kUnsupported) {
    return OutputSkipped{a_target.problem};
  }
  return OutputFailed{a_target.problem};
}

OutputState SurfaceState(const BoundSurfaces *a_bound, const SlotPlan &a_slot,
                         std::string_view a_existing) {
  const bool shell = a_bound && a_bound->shell;
  const bool material = a_bound && a_bound->material;
  if (a_slot.surface == Surface::kShell && !shell) {
    return OutputFailed{std::string{kShellNotCreated}};
  }
  if (a_slot.surface == Surface::kMaterial && !material) {
    return OutputFailed{std::string{kMaterialNotBound}};
  }
  if (!a_existing.empty()) {
    return OutputFailed{std::string{a_existing}};
  }
  if (!a_bound) {
    return OutputFailed{std::string{kSurfaceNotBound}};
  }
  const auto target =
      std::ranges::find_if(a_bound->targets, [&](const SlotTargetState &a_t) {
        return a_t.surface == a_slot.surface && a_t.slot == a_slot.slot;
      });
  if (target == a_bound->targets.end()) {
    return OutputFailed{std::string{kSurfaceNotBound}};
  }
  return TargetState(*target);
}

struct LocatedOutput {
  PlacementId placement{};
  InstanceId instance{};
  const SurfaceOutput &surface;
  PlacedOutputState &state;
};

std::optional<LocatedOutput>
LocateOutput(std::vector<PlacementOutputs> &a_placements,
             const GeometryBinding &a_geometry, const ActorMatches &a_matches,
             std::span<const Recipe> a_store,
             std::span<const InstanceId> a_compiled,
             const SlotContribution &a_contribution) {
  const std::optional<PlacementId> placement =
      PlacementOfContribution(a_geometry.placement, a_contribution.placed);
  const Placement *matched =
      placement ? PlacementAt(a_matches, *placement) : nullptr;
  if (!matched || !std::ranges::contains(a_compiled, matched->instance)) {
    return std::nullopt;
  }
  const Recipe *recipe =
      RecipeOfInstance(a_matches, a_store, matched->instance);
  const SurfaceOutput *surface =
      recipe ? SurfaceOutputOf(*recipe, a_contribution.output) : nullptr;
  PlacedOutputState *state =
      OutputStateAt(a_placements, *placement, a_contribution.output);
  if (!surface || !state) {
    return std::nullopt;
  }
  return LocatedOutput{*placement, matched->instance, *surface, *state};
}

SideRef SideFor(std::vector<TextureSize> &a_sizes, TextureSize a_size) {
  const auto found = std::ranges::find(a_sizes, a_size);
  if (found != a_sizes.end()) {
    return {static_cast<std::uint32_t>(found - a_sizes.begin())};
  }
  a_sizes.push_back(a_size);
  return {static_cast<std::uint32_t>(a_sizes.size() - 1)};
}

std::optional<StepOutputRef> StackOutputFor(const RenderPlan &a_plan,
                                            const StackRequest &a_stack) {
  for (const StackOutputBinding &binding : a_plan.stackOutputs) {
    if (binding.placement == a_stack.placement &&
        binding.output == a_stack.output &&
        binding.geometry == a_stack.geometry) {
      return binding.result;
    }
  }
  return std::nullopt;
}

const GeometryBinding *BindingAt(std::span<const GeometryBinding> a_bound,
                                 GeometryId a_geometry) noexcept {
  const auto found =
      std::ranges::find(a_bound, a_geometry, &GeometryBinding::geometry);
  return found == a_bound.end() ? nullptr : &*found;
}

const CompositeStackStep *CompositeAt(const RenderPlan &a_plan,
                                      StepOutputRef a_result) noexcept {
  if (a_result.step >= a_plan.steps.size()) {
    return nullptr;
  }
  return Get<CompositeStackStep>(a_plan.steps[a_result.step].kind);
}

void SetProblem(std::vector<PlacementOutputs> &a_outputs,
                const StackRequest &a_stack, const std::string &a_problem) {
  if (PlacedOutputState *output =
          OutputStateAt(a_outputs, a_stack.placement, a_stack.output)) {
    output->state = WithProblem(output->state, a_problem);
  }
}

bool HasLightOutput(const Recipe &a_recipe) {
  return std::ranges::any_of(a_recipe.outputs, [](const Output &a_output) {
    return Get<LightOutput>(a_output) != nullptr;
  });
}

void SetLightProblem(std::vector<InstanceLightProblem> &a_problems,
                     InstanceId a_instance, std::string a_problem) {
  const auto found = std::ranges::find(a_problems, a_instance,
                                       &InstanceLightProblem::instance);
  if (found == a_problems.end()) {
    a_problems.push_back({a_instance, std::move(a_problem)});
  } else {
    found->problem = std::move(a_problem);
  }
}

std::optional<InstanceId> LightSource(const ActorLightPlan &a_plan,
                                      LightContributor a_placed) noexcept {
  const std::size_t index = IndexOf(a_placed);
  if (index >= a_plan.sources.size()) {
    return std::nullopt;
  }
  return a_plan.sources[index];
}

std::vector<GeometryId> LightGeometries(const ActorMatches &a_matches,
                                        InstanceId a_instance,
                                        const LightOutput &a_light) {
  std::vector<GeometryId> eligible;
  for (const GeometryId geometry :
       ThirdPersonGeometriesOfInstance(a_matches, a_instance)) {
    const Geometry *found = GeometryAt(a_matches, geometry);
    if (found && LightEligible(*found, a_light)) {
      eligible.push_back(geometry);
    }
  }
  return eligible;
}
}

SurfacePlan PlanSurfaces(const ActorMatches &a_matches,
                         std::span<const Recipe> a_store,
                         const OutputFilter &a_filter) {
  SurfacePlan plan;
  for (std::size_t g = 0; g < a_matches.geometries.size(); ++g) {
    const GeometryId geometry{g};
    GeometryBinding binding{
        geometry,
        PlanGeometryPlacement(a_matches, a_store, geometry, a_filter),
        {},
        {},
        std::nullopt};
    binding.stacks =
        PlanStacks(binding.placement.placed, binding.placement.plan);
    binding.binding =
        PlanBinding(binding.placement.placed, binding.placement.plan);
    binding.shellOwner = ShellOwnerOf(a_matches, a_store, binding);
    for (const PlacementId source : binding.placement.sources) {
      if (const Placement *placement = PlacementAt(a_matches, source)) {
        plan.placements.push_back(
            OutputsOfPlacement(*placement, source, geometry));
      }
    }
    MarkReplaced(plan.placements, binding.placement, a_matches, a_store);
    plan.geometries.push_back(std::move(binding));
  }
  return plan;
}

OutputActivation ActivateOutputs(const SurfacePlan &a_plan,
                                 std::span<const BoundSurfaces> a_bound,
                                 const ActorMatches &a_matches,
                                 std::span<const Recipe> a_store,
                                 std::span<const InstanceId> a_compiled) {
  OutputActivation activation{a_plan.placements, {}, {}};
  for (const GeometryBinding &geometry : a_plan.geometries) {
    const BoundSurfaces *bound = BoundAt(a_bound, geometry.geometry);
    for (const SlotPlan &slot : geometry.placement.plan.slots) {
      SlotChainRequest chain{geometry.geometry, slot.surface, slot.slot, {}};
      for (const SlotContribution &contribution : slot.chain) {
        const std::optional<LocatedOutput> located =
            LocateOutput(activation.placements, geometry, a_matches, a_store,
                         a_compiled, contribution);
        if (!located) {
          continue;
        }
        located->state.state =
            SurfaceState(bound, slot, ProblemOf(located->state.state));
        if (!Is<OutputPending>(located->state.state)) {
          continue;
        }
        activation.stacks.push_back({geometry.geometry, slot.surface, slot.slot,
                                     located->placement, located->instance,
                                     contribution.output,
                                     located->surface.resolution.value_or(
                                         DefaultSlotResolution(slot.slot))});
        chain.links.push_back({located->placement, contribution.output});
      }
      if (chain.links.size() > 1) {
        activation.chains.push_back(std::move(chain));
      }
    }
  }
  return activation;
}

std::expected<RenderBuildRequest, std::string>
RenderBuildRequestOf(const OutputActivation &a_activation,
                     std::span<const TextureSize> a_sizes) {
  if (a_sizes.size() != a_activation.stacks.size()) {
    return std::unexpected{std::format("{} stack sizes for {} stacks",
                                       a_sizes.size(),
                                       a_activation.stacks.size())};
  }
  RenderBuildRequest request{{}, a_activation.chains, {}};
  for (std::size_t i = 0; i < a_activation.stacks.size(); ++i) {
    request.stacks.push_back(
        {a_activation.stacks[i], SideFor(request.full.sizes, a_sizes[i])});
  }
  return request;
}

TextureSize StackSizeOf(const StackSizeFacts &a_facts, Resolution a_slot,
                        Resolution a_scale) {
  const TextureSize largest{a_facts.nativeLargest == 0 ? TextureSize::kMax
                                                       : a_facts.nativeLargest};
  const TextureSize scaled{largest.Pixels() / ResolutionDivisor(a_scale)};
  const TextureSize requested{scaled.Pixels() / ResolutionDivisor(a_slot)};
  if (!a_facts.baseLargest) {
    return requested;
  }
  const std::uint32_t floor = requested.Pixels();
  const std::uint32_t ceiling = std::max(largest.Pixels(), floor);
  return TextureSize{std::clamp(*a_facts.baseLargest, floor, ceiling)};
}

std::vector<GeometrySlot>
UnpublishedSlotsOf(std::span<const GeometryBinding> a_geometries) {
  std::vector<GeometrySlot> slots;
  for (const GeometryBinding &geometry : a_geometries) {
    for (std::size_t s = 0; s < geometry.stacks.slots.size(); ++s) {
      slots.push_back({geometry.geometry, s});
    }
  }
  return slots;
}

PlanRoute PlanRouteOf(const PlanRouteFacts &a_facts) noexcept {
  if (!a_facts.cacheOn) {
    return PlanRoute::kFresh;
  }
  if (!a_facts.keyed) {
    return PlanRoute::kUncacheable;
  }
  if (a_facts.cached) {
    return a_facts.verifies ? PlanRoute::kVerifyCached : PlanRoute::kCached;
  }
  if (a_facts.verifies) {
    return PlanRoute::kFreshStored;
  }
  return a_facts.jobOpen ? PlanRoute::kShareJob : PlanRoute::kSubmitJob;
}

PerLevel<std::optional<LevelCost>> LevelCostsOf(const RenderPlan &a_plan,
                                                const PlanSizes &a_full) {
  PerLevel<std::optional<LevelCost>> costs;
  for (const ResolutionLevel level : kResolutionLevels) {
    const std::expected<PlanSizes, std::string> sizes =
        SizesFor(a_plan, AtLevel(a_full, level).sizes);
    if (!sizes) {
      continue;
    }
    const PlanTargets targets = TargetsOf(a_plan, *sizes);
    costs.At(level) = LevelCost{CostOf(targets.withIntermediates).bytes,
                                CostOf(targets.whenReleased).bytes};
  }
  return costs;
}

std::expected<RenderBuild, std::string>
RenderBuildAt(const RenderPlan &a_plan, const PlanSizes &a_full,
              const InstallBounds &a_bounds) {
  PerLevel<std::optional<LevelCost>> costs = LevelCostsOf(a_plan, a_full);
  ResolutionLevel level = a_bounds.finestLevel;
  std::uint64_t charged = 0;
  if (a_bounds.headroom) {
    LevelBytes bytes;
    for (std::optional<ResolutionLevel> at = a_bounds.finestLevel; at;
         at = CoarserLevel(*at)) {
      if (const std::optional<LevelCost> &cost = costs.At(*at)) {
        bytes.At(*at) = cost->withIntermediates;
      }
    }
    level = FittingLevel(a_bounds.finestLevel, bytes, a_bounds.headroom);
    charged = bytes.At(level).value_or(0);
  }
  std::expected<PlanSizes, std::string> sizes =
      SizesFor(a_plan, AtLevel(a_full, level).sizes);
  if (!sizes) {
    return std::unexpected{std::move(sizes.error())};
  }
  return RenderBuild{level, charged, std::move(*sizes), costs};
}

Attachments AttachmentsOf(const RenderPlan &a_plan,
                          const RenderBuildRequest &a_request,
                          std::span<const GeometryBinding> a_bound,
                          std::vector<PlacementOutputs> a_outputs) {
  Attachments attachments{{}, {}, std::move(a_outputs)};
  for (std::size_t i = 0; i < a_request.stacks.size(); ++i) {
    const StackRequest &stack = a_request.stacks[i].request;
    if (const std::optional<StepOutputRef> result =
            StackOutputFor(a_plan, stack)) {
      attachments.stacks.push_back({i, *result});
    } else {
      SetProblem(attachments.placements, stack, std::string{kStackNotLowered});
    }
  }
  for (const MergedChainBinding &merged : a_plan.mergedChains) {
    const GeometryBinding *geometry = BindingAt(a_bound, merged.geometry);
    const CompositeStackStep *step = CompositeAt(a_plan, merged.result);
    if (!geometry || !step) {
      continue;
    }
    const SlotStackPlan *slot =
        SlotStackPlanOf(geometry->stacks, merged.surface, merged.slot);
    attachments.merged.push_back(
        {merged.geometry, merged.surface, merged.slot, merged.links,
         merged.layerStarts, merged.layerCount, merged.result,
         step->requirements.side, slot && slot->animated});
  }
  return attachments;
}

std::vector<PlacementOutputs>
FailedOutputs(std::vector<PlacementOutputs> a_outputs,
              std::span<const StackRequest> a_stacks,
              const std::string &a_problem) {
  for (const StackRequest &stack : a_stacks) {
    SetProblem(a_outputs, stack, a_problem);
  }
  return a_outputs;
}

LightProgram PlanLightsOf(const ActorMatches &a_matches,
                          std::span<const Recipe> a_store,
                          const OutputFilter &a_filter) {
  const ActorLightPlan plan = PlanActorLights(a_matches, a_store, a_filter);
  LightProgram program;
  for (const InstanceId source : plan.sources) {
    const Recipe *recipe = RecipeOfInstance(a_matches, a_store, source);
    if (recipe && HasLightOutput(*recipe)) {
      SetLightProblem(program.problems, source, std::string{kNoEligibleLight});
    }
  }
  for (const LightContribution &replaced : plan.plan.replaced) {
    const std::optional<InstanceId> source = LightSource(plan, replaced.placed);
    const std::optional<std::size_t> replacer = ReplacerOf(plan.plan, replaced);
    if (!source || !replacer || *replacer >= plan.placed.size() ||
        !plan.placed[*replacer].recipe) {
      continue;
    }
    SetLightProblem(program.problems, *source,
                    std::format("actor-wide lights replaced by recipe {}",
                                plan.placed[*replacer].recipe->id));
  }
  for (const LightContribution &shown : plan.plan.shown) {
    const std::optional<InstanceId> source = LightSource(plan, shown.placed);
    const Recipe *recipe =
        source ? RecipeOfInstance(a_matches, a_store, *source) : nullptr;
    const LightOutput *light =
        recipe ? LightOutputOf(*recipe, shown.output) : nullptr;
    if (!light) {
      continue;
    }
    std::vector<GeometryId> geometries =
        LightGeometries(a_matches, *source, *light);
    if (!geometries.empty()) {
      program.lights.push_back({*source, shown.output, std::move(geometries)});
    }
  }
  return program;
}
}
