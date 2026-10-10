// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InstallProgram.h"
#include "test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
SurfaceOutput Emissive(Surface a_surface = Surface::kMaterial) {
  SurfaceOutput output;
  output.surface = a_surface;
  output.slot = Slot::kEmissive;
  return output;
}

Recipe RecipeWith(std::string a_id, std::vector<Output> a_outputs,
                  int a_priority = 0) {
  Recipe recipe;
  recipe.id = std::move(a_id);
  recipe.keys.push_back(RecipeKey{});
  recipe.outputs = std::move(a_outputs);
  recipe.priority = a_priority;
  return recipe;
}

std::vector<Geometry> Body() {
  Geometry body;
  body.identity.name = "body01";
  body.identity.diffusePath = "body.dds";
  body.keys.diffusePaths.push_back("body.dds");
  return {body};
}

struct Fixture {
  std::vector<Recipe> store;
  ActorMatches matches;
};

Fixture Matched(std::vector<Recipe> a_store) {
  Fixture fixture{std::move(a_store), {}};
  fixture.matches = MatchActor(Body(), fixture.store);
  return fixture;
}

std::vector<InstanceId> AllInstances(const ActorMatches &a_matches) {
  std::vector<InstanceId> instances;
  for (std::size_t i = 0; i < a_matches.instances.size(); ++i) {
    instances.push_back(InstanceId{i});
  }
  return instances;
}

BoundSurfaces MaterialBound(std::string a_targetProblem = {}) {
  return {GeometryId{0},
          true,
          false,
          {{Surface::kMaterial, Slot::kEmissive, std::move(a_targetProblem)}}};
}

const PlacedOutputState *OutputOf(std::span<const PlacementOutputs> a_outputs,
                                  std::size_t a_placement) {
  const auto found = std::ranges::find(a_outputs, PlacementId{a_placement},
                                       &PlacementOutputs::placement);
  if (found == a_outputs.end() || found->outputs.empty()) {
    return nullptr;
  }
  return &found->outputs.front();
}

std::optional<std::size_t> PlacementOfRecipe(const Fixture &a_fixture,
                                             std::string_view a_id) {
  for (std::size_t p = 0; p < a_fixture.matches.placements.size(); ++p) {
    const Instance *instance =
        InstanceAt(a_fixture.matches, a_fixture.matches.placements[p].instance);
    const std::size_t recipe = instance ? IndexOf(instance->recipe) : 0;
    if (instance && recipe < a_fixture.store.size() &&
        a_fixture.store[recipe].id == a_id) {
      return p;
    }
  }
  return std::nullopt;
}

RenderPlan StackPlanAt(SideRef a_side) {
  RenderPlan plan;
  CompositeStackStep stack{};
  stack.requirements.side = a_side;
  plan.steps.push_back({stack, "stack"});
  plan.stackOutputs.push_back(
      {PlacementId{0}, 0, GeometryId{0}, StepOutputRef{0, 0}});
  return plan;
}

void AMaterialOutputBindsTheMaterial() {
  const Fixture fixture = Matched({RecipeWith("glow", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  Check(plan.geometries.size() == 1 && plan.geometries[0].binding.material &&
            !plan.geometries[0].binding.shell && !plan.geometries[0].shellOwner,
        "a material output binds only the material");
  const PlacedOutputState *output = OutputOf(plan.placements, 0);
  Check(output && !IsActive(output->state),
        "an output starts inactive before the surfaces are bound");
}

void AShellOutputNamesItsOwner() {
  const Fixture fixture =
      Matched({RecipeWith("veil", {Emissive(Surface::kShell)})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  Check(plan.geometries.size() == 1 && plan.geometries[0].binding.shell &&
            plan.geometries[0].shellOwner == InstanceId{0},
        "a shell output binds a shell owned by its instance");
}

void AReplacedOutputNamesItsReplacer() {
  SurfaceOutput replacing = Emissive();
  replacing.replace = true;
  const Fixture fixture = Matched(
      {RecipeWith("glow", {Emissive()}), RecipeWith("shine", {replacing}, 5)});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  const std::optional<std::size_t> glow = PlacementOfRecipe(fixture, "glow");
  const PlacedOutputState *output =
      glow ? OutputOf(plan.placements, *glow) : nullptr;
  Check(output && ProblemOf(output->state) == "replaced by recipe shine",
        "a replaced output names the recipe that replaced it");
}

void ABoundSurfaceActivatesItsOutput() {
  const Fixture fixture = Matched({RecipeWith("glow", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  const std::vector<BoundSurfaces> bound{MaterialBound()};
  const OutputActivation activation =
      ActivateOutputs(plan, bound, fixture.matches, fixture.store,
                      AllInstances(fixture.matches));
  const PlacedOutputState *output = OutputOf(activation.placements, 0);
  Check(output && Is<OutputPending>(output->state),
        "an output on a bound surface is pending its render");
  Check(activation.stacks.size() == 1 &&
            activation.stacks[0].geometry == GeometryId{0} &&
            activation.stacks[0].instance == InstanceId{0} &&
            activation.stacks[0].resolution ==
                DefaultSlotResolution(Slot::kEmissive),
        "an active output requests one stack at the slot's resolution");
  Check(activation.chains.empty(), "a single contribution forms no chain");
}

void AFailedMaterialBlocksItsOutputs() {
  const Fixture fixture = Matched({RecipeWith("glow", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  BoundSurfaces failed = MaterialBound();
  failed.material = false;
  const std::vector<BoundSurfaces> bound{failed};
  const OutputActivation activation =
      ActivateOutputs(plan, bound, fixture.matches, fixture.store,
                      AllInstances(fixture.matches));
  const PlacedOutputState *output = OutputOf(activation.placements, 0);
  Check(output && Is<OutputFailed>(output->state) &&
            ProblemOf(output->state) == kMaterialNotBound,
        "an output on a failed material fails with the binding problem");
  Check(activation.stacks.empty(), "a failed output requests no stack");
}

void ATargetProblemBlocksItsOutput() {
  const Fixture fixture = Matched({RecipeWith("glow", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  const std::vector<BoundSurfaces> problem{MaterialBound("slot is shared")};
  const OutputActivation blocked =
      ActivateOutputs(plan, problem, fixture.matches, fixture.store,
                      AllInstances(fixture.matches));
  const PlacedOutputState *output = OutputOf(blocked.placements, 0);
  Check(output && ProblemOf(output->state) == "slot is shared",
        "a slot target's problem reaches the output");
  BoundSurfaces untargeted = MaterialBound();
  untargeted.targets.clear();
  const std::vector<BoundSurfaces> none{untargeted};
  const OutputActivation unbound =
      ActivateOutputs(plan, none, fixture.matches, fixture.store,
                      AllInstances(fixture.matches));
  output = OutputOf(unbound.placements, 0);
  Check(output && ProblemOf(output->state) == kSurfaceNotBound,
        "a slot without a target reports an unbound surface");
}

void AnUncompiledInstanceStaysInactive() {
  const Fixture fixture = Matched({RecipeWith("glow", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  const std::vector<BoundSurfaces> bound{MaterialBound()};
  const OutputActivation activation =
      ActivateOutputs(plan, bound, fixture.matches, fixture.store, {});
  const PlacedOutputState *output = OutputOf(activation.placements, 0);
  Check(output && !IsActive(output->state) && activation.stacks.empty(),
        "an instance without a graph activates nothing");
}

void LayeredOutputsFormAChain() {
  const Fixture fixture = Matched(
      {RecipeWith("glow", {Emissive()}), RecipeWith("pulse", {Emissive()})});
  const SurfacePlan plan = PlanSurfaces(fixture.matches, fixture.store);
  const std::vector<BoundSurfaces> bound{MaterialBound()};
  const OutputActivation activation =
      ActivateOutputs(plan, bound, fixture.matches, fixture.store,
                      AllInstances(fixture.matches));
  Equal(activation.stacks.size(), std::size_t{2},
        "two layered outputs request two stacks");
  Check(activation.chains.size() == 1 && activation.chains[0].links.size() == 2,
        "two layered outputs form one chain");
}

void EqualSizesShareASide() {
  OutputActivation activation;
  activation.stacks.resize(3);
  const std::vector<TextureSize> sizes{TextureSize{512}, TextureSize{256},
                                       TextureSize{512}};
  const std::expected<RenderBuildRequest, std::string> request =
      RenderBuildRequestOf(activation, sizes);
  Check(request && request->full.sizes.size() == 2 &&
            request->stacks[0].side == request->stacks[2].side &&
            request->stacks[1].side == SideRef{1},
        "stacks of one size share a side");
  const std::vector<TextureSize> shortList{TextureSize{512}};
  Check(!RenderBuildRequestOf(activation, shortList),
        "a size list that does not cover every stack is refused");
}

void TheBuildFitsTheHeadroom() {
  const RenderPlan plan = StackPlanAt(SideRef{0});
  const PlanSizes full{{TextureSize{1024}}};
  const PerLevel<std::optional<LevelCost>> costs = LevelCostsOf(plan, full);
  const std::optional<LevelCost> &fullCost = costs.At(ResolutionLevel::kFull);
  const std::optional<LevelCost> &halfCost = costs.At(ResolutionLevel::kHalf);
  Check(fullCost && halfCost &&
            fullCost->withIntermediates > halfCost->withIntermediates,
        "a coarser level costs less");
  const std::expected<RenderBuild, std::string> player =
      RenderBuildAt(plan, full, {ResolutionLevel::kFull, std::nullopt});
  Check(player && player->level == ResolutionLevel::kFull &&
            player->chargedBytes == 0,
        "a build without a headroom takes the finest level and charges none");
  if (!fullCost || !halfCost) {
    return;
  }
  const std::expected<RenderBuild, std::string> fitted = RenderBuildAt(
      plan, full, {ResolutionLevel::kFull, fullCost->withIntermediates - 1});
  Check(fitted && fitted->level == ResolutionLevel::kHalf &&
            fitted->chargedBytes == halfCost->withIntermediates,
        "a build steps down until it fits the headroom");
  Check(fitted && fitted->sizes.sizes.size() == 1 &&
            fitted->sizes.sizes[0] == TextureSize{512},
        "the build sizes follow the chosen level");
}

void LoweredStacksAttach() {
  StackRequest lowered;
  lowered.placement = PlacementId{0};
  StackRequest dropped;
  dropped.placement = PlacementId{1};
  RenderBuildRequest request{
      {{lowered, SideRef{0}}, {dropped, SideRef{0}}}, {}, {}};
  RenderPlan plan;
  plan.stackOutputs.push_back(
      {PlacementId{0}, 0, GeometryId{0}, StepOutputRef{4, 0}});
  std::vector<PlacementOutputs> outputs{
      {PlacementId{0}, GeometryId{0}, {{0, OutputPending{}}}},
      {PlacementId{1}, GeometryId{0}, {{0, OutputPending{}}}}};
  const Attachments attached =
      AttachmentsOf(plan, request, {}, std::move(outputs));
  Check(attached.stacks.size() == 1 && attached.stacks[0].stack == 0 &&
            attached.stacks[0].result == StepOutputRef{4, 0},
        "a lowered stack attaches to its plan output");
  const PlacedOutputState *missing = OutputOf(attached.placements, 1);
  Check(missing && Is<OutputFailed>(missing->state) &&
            ProblemOf(missing->state) == kStackNotLowered,
        "a stack the plan did not lower fails its output");
}

void MergedChainsAttachToTheirGeometry() {
  RenderPlan plan;
  CompositeStackStep composite{};
  composite.requirements.side = SideRef{2};
  plan.steps.push_back({composite, "stack"});
  MergedChainBinding merged;
  merged.geometry = GeometryId{0};
  merged.result = StepOutputRef{0, 0};
  plan.mergedChains.push_back(merged);
  MergedChainBinding stray = merged;
  stray.geometry = GeometryId{9};
  plan.mergedChains.push_back(stray);
  std::vector<GeometryBinding> bound(1);
  bound[0].geometry = GeometryId{0};
  const Attachments attached = AttachmentsOf(plan, {}, bound, {});
  Check(attached.merged.size() == 1 &&
            attached.merged[0].geometry == GeometryId{0} &&
            attached.merged[0].side == SideRef{2},
        "a merged chain attaches to its geometry at its stack's side");
}

void AFailedPlanFailsEveryRequestedOutput() {
  std::vector<PlacementOutputs> outputs{
      {PlacementId{0}, GeometryId{0}, {{0, OutputPending{}}}}};
  StackRequest stack;
  const std::vector<StackRequest> stacks{stack};
  const std::vector<PlacementOutputs> failed =
      FailedOutputs(std::move(outputs), stacks, "no render plan");
  const PlacedOutputState *output = OutputOf(failed, 0);
  Check(output && ProblemOf(output->state) == "no render plan",
        "a failed plan names its problem on every requested output");
}

void ALightRecipePlacesALight() {
  const Fixture fixture = Matched(
      {RecipeWith("glow", {Emissive()}), RecipeWith("lamp", {LightOutput{}})});
  const LightProgram lights = PlanLightsOf(fixture.matches, fixture.store);
  Check(lights.lights.size() == 1 && lights.lights[0].output == 0 &&
            lights.lights[0].geometries ==
                std::vector<GeometryId>{GeometryId{0}},
        "a light recipe places one light on its geometry");
  Check(lights.problems.size() == 1 &&
            lights.problems[0].problem == kNoEligibleLight,
        "a light instance starts with a problem until the engine places it");
}
}

int main() {
  AMaterialOutputBindsTheMaterial();
  AShellOutputNamesItsOwner();
  AReplacedOutputNamesItsReplacer();
  ABoundSurfaceActivatesItsOutput();
  AFailedMaterialBlocksItsOutputs();
  ATargetProblemBlocksItsOutput();
  AnUncompiledInstanceStaysInactive();
  LayeredOutputsFormAChain();
  EqualSizesShareASide();
  TheBuildFitsTheHeadroom();
  LoweredStacksAttach();
  MergedChainsAttachToTheirGeometry();
  AFailedPlanFailsEveryRequestedOutput();
  ALightRecipePlacesALight();
  return test::Finish("planners_installprogram");
}
