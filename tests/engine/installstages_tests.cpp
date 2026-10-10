// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/InstallStages.h"
#include "test_support.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
SurfaceOutput Emissive() {
  SurfaceOutput output;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  return output;
}

Recipe RecipeWith(std::string a_id) {
  Recipe recipe;
  recipe.id = std::move(a_id);
  recipe.keys.push_back(RecipeKey{});
  recipe.outputs = {Emissive()};
  return recipe;
}

struct Fixture {
  std::vector<Recipe> store;
  LiveActor actor;
};

Fixture Matched() {
  Fixture fixture;
  fixture.store = {RecipeWith("glow"), RecipeWith("pulse")};
  Geometry body;
  body.identity.name = "body01";
  body.identity.diffusePath = "body.dds";
  body.keys.diffusePaths.push_back("body.dds");
  const std::vector<Geometry> geometries{body};
  fixture.actor.matches = MatchActor(geometries, fixture.store);
  fixture.actor.instances.resize(fixture.actor.matches.instances.size());
  fixture.actor.pieces.resize(1);
  fixture.actor.pieces[0].geometries.resize(1);
  return fixture;
}

std::vector<InstanceId> AllInstances(const LiveActor &a_actor) {
  std::vector<InstanceId> instances;
  for (std::size_t i = 0; i < a_actor.matches.instances.size(); ++i) {
    instances.push_back(InstanceId{i});
  }
  return instances;
}

void AKeptPlanReadsBack() {
  Fixture fixture = Matched();
  const SurfacePlan planned =
      PlanSurfaces(fixture.actor.matches, fixture.store);
  KeepSurfacePlan(fixture.actor, planned);
  Equal(fixture.actor.placements.size(),
        fixture.actor.matches.placements.size(),
        "every matched placement has a live record");
  const SurfacePlan kept = SurfacePlanOf(fixture.actor);
  Check(kept.geometries.size() == 1 && planned.geometries.size() == 1,
        "the kept plan has the actor's one geometry");
  if (kept.geometries.size() != 1 || planned.geometries.size() != 1) {
    return;
  }
  const GeometryBinding &before = planned.geometries[0];
  const GeometryBinding &after = kept.geometries[0];
  Check(after.geometry == before.geometry &&
            after.placement.sources == before.placement.sources &&
            after.placement.plan.slots.size() ==
                before.placement.plan.slots.size() &&
            after.stacks == before.stacks && after.binding == before.binding,
        "a kept geometry reads back its plan");
  Check(after.placement.placed.empty(),
        "the kept plan holds no pointer into the catalog");
  Check(kept.placements == planned.placements,
        "the kept outputs read back in the planned order");
}

void ARetiredGeometryRequestsNoStack() {
  Fixture fixture = Matched();
  KeepSurfacePlan(fixture.actor,
                  PlanSurfaces(fixture.actor.matches, fixture.store));
  LiveGeometry &geometry = fixture.actor.pieces[0].geometries[0];
  geometry.material = std::make_unique<MaterialBinding>();
  RetireGeometry(fixture.actor, geometry);
  const SurfacePlan kept = SurfacePlanOf(fixture.actor);
  const OutputActivation activation = ActivateOutputs(
      kept, BoundSurfacesOf(fixture.actor), fixture.actor.matches,
      fixture.store, AllInstances(fixture.actor));
  Check(activation.stacks.empty() && activation.chains.empty(),
        "a retired geometry requests no stack");
  Check(!activation.placements.empty() &&
            ProblemOf(activation.placements[0].outputs[0].state) ==
                kRetiredProblem,
        "a retired geometry's outputs keep the retire problem");
}

void BoundSurfacesReportTheirTargets() {
  Fixture fixture = Matched();
  KeepSurfacePlan(fixture.actor,
                  PlanSurfaces(fixture.actor.matches, fixture.store));
  auto material = std::make_unique<MaterialBinding>();
  material->problem = "slot is shared";
  fixture.actor.pieces[0].geometries[0].material = std::move(material);
  const std::vector<BoundSurfaces> bound = BoundSurfacesOf(fixture.actor);
  Check(bound.size() == 1 && bound[0].material && !bound[0].shell &&
            bound[0].targets.size() == 1 &&
            bound[0].targets[0].problem == "slot is shared",
        "a bound material reports its slot problem");
}

void WrittenOutputsReachKnownPlacements() {
  Fixture fixture = Matched();
  KeepSurfacePlan(fixture.actor,
                  PlanSurfaces(fixture.actor.matches, fixture.store));
  const std::vector<PlacementOutputs> written{
      {PlacementId{0}, GeometryId{0}, {{0, OutputFailed{"no render plan"}}}},
      {PlacementId{99}, GeometryId{0}, {{0, OutputRendered{}}}},
      {PlacementId{1}, GeometryId{0}, {{7, OutputRendered{}}}}};
  WriteOutputs(fixture.actor, written);
  Check(ProblemOf(fixture.actor.placements[0].outputs[0].state) ==
            "no render plan",
        "a written state reaches its output");
  Check(!IsActive(fixture.actor.placements[1].outputs[0].state),
        "unknown placements and outputs are ignored");
}

void CompiledInstancesHaveAGraph() {
  LiveActor actor;
  actor.instances.resize(3);
  actor.instances[0].recipe = std::make_shared<const Recipe>();
  actor.instances[2].recipe = std::make_shared<const Recipe>();
  actor.instances[2].graph =
      std::make_shared<const RecipeGraph>(RecipeGraph::Compile(Recipe{}));
  Check(CompiledInstancesOf(actor) == std::vector<InstanceId>{InstanceId{2}},
        "only an instance with a recipe and a graph is compiled");
}

void TheTextureScaleIsAResolution() {
  Check(ScaleResolutionOf(TextureScale::kQuarter) == Resolution::kQuarter &&
            ScaleResolutionOf(TextureScale::kHalf) == Resolution::kHalf &&
            ScaleResolutionOf(TextureScale::kFull) == Resolution::kFull,
        "each texture scale divides like the resolution of its name");
}
}

int main() {
  AKeptPlanReadsBack();
  ARetiredGeometryRequestsNoStack();
  BoundSurfacesReportTheirTargets();
  WrittenOutputsReachKnownPlacements();
  CompiledInstancesHaveAGraph();
  TheTextureScaleIsAResolution();
  return test::Finish("engine_installstages");
}
