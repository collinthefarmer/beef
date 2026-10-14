// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/IdentityFacts.h"
#include "planners/RenderPlanJob.h"
#include "test_support.h"

#include <memory>
#include <string>
#include <utility>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
Recipe MaterialEmissive() {
  Recipe recipe;
  recipe.id = "material-emissive";
  recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  recipe.masks = {{"dim", "@s * 0.5"}};
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  Layer layer;
  layer.source = Ref{"dim"};
  layer.mask = Ref{"dim"};
  surface.stack.push_back(layer);
  recipe.outputs.push_back(surface);
  return recipe;
}

IdentityFacts FactsOf(std::string a_mesh) {
  IdentityFacts facts;
  facts.instance = InstanceIdentity(1, 1, 0x14);
  facts.mesh = std::move(a_mesh);
  facts.material = "material:a";
  facts.geometry = 11;
  facts.root = 12;
  facts.itemHash = 0.5f;
  facts.shader = {"0x1~A.esp", "Effects/Fill.dds", "Effects/Membrane.dds"};
  return facts;
}

PlanBuildInput InputOf(const std::shared_ptr<const Recipe> &a_recipe,
                       std::string a_mesh) {
  const std::shared_ptr<const RecipeGraph> graph =
      std::make_shared<const RecipeGraph>(RecipeGraph::Compile(*a_recipe));
  const SurfaceOutput *surface = Get<SurfaceOutput>(a_recipe->outputs[0]);
  PlanBuildInput input;
  input.requests.push_back(
      {graph, std::shared_ptr<const SurfaceOutput>{a_recipe, surface},
       a_recipe->id, 0, PlacementId{0}, GeometryId{0}, 1, SideRef{0},
       std::make_shared<const IdentityFacts>(FactsOf(std::move(a_mesh)))});
  return input;
}

void APlanBuildsFromOwnedInputs() {
  const auto recipe = std::make_shared<const Recipe>(MaterialEmissive());
  const PlanBuildOutput output = BuildPlanFrom(InputOf(recipe, "mesh:1"));
  Check(output.plan.has_value(), "the owned input lowers to a plan");
  if (!output.plan) {
    return;
  }
  Equal(output.plan->stackOutputs.size(), std::size_t{1},
        "the plan binds the requested stack");
  Check(output.phases.demands > 0, "sample-dependent layers become demands");
  Check(output.warnings.empty(), "a clean recipe has no warnings");
  const PlanBuildOutput again = BuildPlanFrom(InputOf(recipe, "mesh:1"));
  Check(again.plan && SameStructure(*output.plan, *again.plan),
        "the same input lowers to the same plan");
}

void TheInputOutlivesItsSources() {
  PlanBuildInput input;
  {
    const auto recipe = std::make_shared<const Recipe>(MaterialEmissive());
    input = InputOf(recipe, "mesh:1");
  }
  const PlanBuildOutput output = BuildPlanFrom(input);
  Check(output.plan.has_value(),
        "the input owns its recipe output and graph after the caller drops "
        "them");
}

void RequestsWithoutIdentityGraphOrOutputStillBuild() {
  const auto recipe = std::make_shared<const Recipe>(MaterialEmissive());
  PlanBuildInput input = InputOf(recipe, "mesh:1");
  input.requests[0].identity = nullptr;
  Check(BuildPlanFrom(input).plan.has_value(),
        "a request without identity facts still lowers");
  input.requests[0].graph = nullptr;
  input.requests[0].output = nullptr;
  const PlanBuildOutput output = BuildPlanFrom(input);
  Check(output.phases.demands == 0,
        "a request without a graph or output collects no demand");
}

void IdentityBindingsReadOnlyTheFacts() {
  const ValueBindings bindings = IdentityBindings(
      std::make_shared<const IdentityFacts>(FactsOf("mesh:9")));
  Check(bindings.external(ExternalSource{GeometryInput{}}) ==
            std::string{"mesh:9"},
        "the geometry input names the mesh");
  Check(bindings.external(ExternalSource{ItemInput{ItemField::kChartCount}}) ==
            std::string{"charts:mesh:9"},
        "the chart count names the mesh");
  Check(bindings.external(ExternalSource{MaterialInput{}}) ==
            std::string{"material:a"},
        "the material input names the material");
  Check(bindings.external(ExternalSource{NodePositionInput{"Head"}}) ==
            std::string{"11:12:Head"},
        "a node position names the geometry, the root and the node");
  Check(bindings.external(ExternalSource{TextureInput{"A/B.dds"}}) ==
            std::string{"a/b.dds"},
        "a texture input names its cache key");
  Check(bindings.external(ExternalSource{MatchedShaderInput{}}) ==
            std::string{"efsh:matched:0x1~A.esp"},
        "the matched shader names the resolved shader form");
  Check(bindings.external(ExternalSource{MatchedShaderTextureInput{
            EffectShaderTexture::kFill}}) == std::string{"effects/fill.dds"},
        "the matched fill texture names its resolved path's cache key");
  Check(bindings.external(ExternalSource{
            MatchedShaderTextureInput{EffectShaderTexture::kMembrane}}) ==
            std::string{"effects/membrane.dds"},
        "the matched membrane texture names its resolved path's cache key");
  Equal(bindings.state(3), InstanceIdentity(1, 1, 0x14) + ":3",
        "state is scoped to the instance");
  Equal(InstanceIdentity(1, 2, 0x14), std::string{"1:2:00000014"},
        "the instance names the graph, the context and the actor");
  const ValueBindings empty = IdentityBindings(nullptr);
  Check(empty.external(ExternalSource{GeometryInput{}}).has_value(),
        "missing facts give empty identities instead of a crash");
}
}

int main() {
  APlanBuildsFromOwnedInputs();
  TheInputOutlivesItsSources();
  RequestsWithoutIdentityGraphOrOutputStillBuild();
  IdentityBindingsReadOnlyTheFacts();
  return test::Finish("planners_renderplanjob");
}
