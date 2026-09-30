// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderExecution.h"
#include "recipe/Signals.h"
#include "test_support.h"
#include <iostream>
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
}
int main() {
  Recipe recipe;
  recipe.sources = {{"surface", MaterialSource{MaterialChannel::kDiffuseRgb}}};
  recipe.masks = {{"mask", "dot(@surface,[0.2,0.3,0.5])"}};
  SurfaceOutput surface;
  Layer layer;
  layer.source = Ref{"mask"};
  layer.curve = CurveRef{"x / max(mean,0.01)"};
  surface.stack.push_back(layer);
  recipe.outputs.push_back(surface);
  const auto graph = RecipeGraph::Compile(recipe);
  const std::array requests{RenderStackRequest{
      &graph, &surface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  const auto plan = BuildRenderPlan({}, requests, Bindings());
  if (!plan)
    std::cerr << plan.error() << '\n';
  Check(plan.has_value(), "lower a measured mask into an executable plan");
  if (!plan)
    return test::Finish("render plan lowering");
  Check(ValidateRenderPlan(*plan).has_value(),
        "lowered plan is typed and topological");
  std::size_t reductions = 0, lookups = 0, maps = 0;
  for (std::size_t i = 0; i < plan->steps.size(); ++i) {
    const auto &kind = plan->steps[i].kind;
    if (const auto *reduction = Get<SubmitReductionStep>(kind)) {
      ++reductions;
      const auto *producer = Get<StepOutputRef>(reduction->value);
      Check(producer && producer->step < i,
            "reduction follows the current field producer");
      if (producer && producer->step < plan->steps.size()) {
        const auto *field =
            Get<EvaluateProgramStep>(plan->steps[producer->step].kind);
        Check(field &&
                  field->requirements.format == TextureFormat::kRgba32Float,
              "measurement expression uses a float target");
      }
    }
    if (const auto *lookup = Get<BuildLookupStep>(kind)) {
      ++lookups;
      Check(lookup->boundArguments.size() == 1,
            "lookup has an explicit bound argument");
      if (!lookup->boundArguments.empty()) {
        const auto *ref =
            Get<RenderInputRef>(lookup->boundArguments.front().value);
        const auto *measurement =
            ref && ref->input < plan->inputs.size()
                ? Get<ReadbackBinding>(plan->inputs[ref->input].binding)
                : nullptr;
        Check(measurement && measurement->submission < i &&
                  Is<SubmitReductionStep>(
                      plan->steps[measurement->submission].kind),
              "lookup reads the measurement of a reduction submission");
      }
    }
    maps += Is<MapFieldStep>(kind);
  }
  Check(reductions == 1 && lookups == 1 && maps == 1,
        "scalar mapping lowers once per operation");
  recipe.outputs.clear();
  surface.stack.front().source = Ref{"surface"};
  recipe.outputs.push_back(surface);
  const auto vectorGraph = RecipeGraph::Compile(recipe);
  const std::array vectorRequests{RenderStackRequest{
      &vectorGraph, &surface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  const auto vectorPlan = BuildRenderPlan({}, vectorRequests, Bindings());
  if (!vectorPlan)
    std::cerr << vectorPlan.error() << '\n';
  Check(vectorPlan.has_value(),
        "vector compatibility curve lowers through scalar projections");
  if (vectorPlan) {
    std::size_t sharedLookups = 0;
    for (const auto &step : vectorPlan->steps)
      sharedLookups += Is<BuildLookupStep>(step.kind);
    Check(sharedLookups == 1,
          "vector components share the same measured lookup");
  }
  RenderExecution<int> execution{*plan};
  std::optional<StepOutputRef> mapped;
  std::optional<RenderInputId> material, measured;
  for (std::size_t i = 0; i < plan->steps.size(); ++i)
    if (Is<MapFieldStep>(plan->steps[i].kind))
      mapped = StepOutputRef{i, 0};
  for (std::size_t i = 0; i < plan->inputs.size(); ++i) {
    if (plan->inputs[i].type == RenderValueType{RenderResourceType::kMaterial})
      material = i;
    if (Is<ReadbackBinding>(plan->inputs[i].binding))
      measured = i;
    else
      Check(execution.SetInput(i, 8, true).has_value(),
            "bind fake backend inputs");
  }
  int submissions = 0, builtLookups = 0, mappings = 0, submittedField = 0;
  bool failSubmission = false;
  const auto backend =
      [&](const RenderStep &step,
          std::span<const ResolvedRenderInput<int>> inputs,
          std::monostate &) -> std::expected<int, std::string> {
    if (Is<SubmitReductionStep>(step.kind)) {
      ++submissions;
      if (failSubmission)
        return std::unexpected("reduction could not be drawn");
      submittedField = inputs.front().value;
      return 0;
    }
    if (Is<BuildLookupStep>(step.kind))
      ++builtLookups;
    if (Is<MapFieldStep>(step.kind))
      ++mappings;
    return inputs.empty() ? 8 : inputs.front().value;
  };
  const auto deliver = [&] {
    const int value = submittedField / 2;
    const auto &current = execution.Inputs()[*measured].value;
    return execution.SetInput(*measured, value, !current || *current != value)
        .has_value();
  };
  const auto equal = [](int a, int b) { return a == b; };
  const auto select = [](const CompositeStackStep &step, const int &)
      -> std::expected<std::vector<RenderValueRef>, std::string> {
    return InputsOf(RenderStepKind{step});
  };
  Check(mapped && material && measured,
        "measured mapping exposes its material, measurement and output");
  if (mapped && material && measured) {
    Check(!execution.Evaluate(*mapped, backend, equal, select) &&
              submissions == 1 && builtLookups == 0,
          "reading the measurement submits the field; consumers wait for the "
          "first measurement");
    Check(deliver() &&
              execution.Evaluate(*mapped, backend, equal, select).has_value() &&
              builtLookups == 1 && mappings == 1,
          "the first delivered measurement builds the lookup and maps");
    Check(execution.SetInput(*material, 9, true).has_value(),
          "replace material contents");
    Check(execution.Evaluate(*mapped, backend, equal, select).has_value() &&
              submissions == 2 && builtLookups == 1 && mappings == 2,
          "a changed field is submitted and remapped with the last "
          "measurement");
    Check(deliver() &&
              execution.Evaluate(*mapped, backend, equal, select).has_value() &&
              builtLookups == 1 && mappings == 2,
          "an unchanged measurement leaves the lookup and mapping cached");
    Check(execution.SetInput(*material, 10, true).has_value() &&
              execution.Evaluate(*mapped, backend, equal, select).has_value() &&
              deliver() &&
              execution.Evaluate(*mapped, backend, equal, select).has_value() &&
              builtLookups == 2 && mappings == 4,
          "a changed measurement rebuilds the lookup before mapping");
    Check(execution.SetInput(*material, 12, true).has_value(),
          "invalidate field before a failed submission");
    failSubmission = true;
    Check(!execution.Evaluate(*mapped, backend, equal, select) &&
              builtLookups == 2 && mappings == 4,
          "a failed submission blocks consumers");
  }
  Recipe clusterRecipe;
  clusterRecipe.sources = {{"zones", MaterialClustersSource{}}};
  SurfaceOutput clusterSurface;
  Layer clusterLayer;
  clusterLayer.source = Ref{"zones"};
  clusterSurface.stack.push_back(clusterLayer);
  clusterRecipe.outputs.push_back(clusterSurface);
  const auto clusterGraph = RecipeGraph::Compile(clusterRecipe);
  const std::array clusterRequests{RenderStackRequest{
      &clusterGraph, &clusterSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  const auto clusterPlan = BuildRenderPlan({}, clusterRequests, Bindings());
  Check(clusterPlan.has_value(), "material clusters lower into a plan");
  if (clusterPlan) {
    bool readsSample = false;
    for (const auto &step : clusterPlan->steps)
      if (const auto *analysis = Get<ClusterMaterialStep>(step.kind)) {
        const auto *ref = Get<RenderInputRef>(analysis->sample);
        const auto *readback =
            ref && ref->input < clusterPlan->inputs.size()
                ? Get<ReadbackBinding>(clusterPlan->inputs[ref->input].binding)
                : nullptr;
        readsSample = readback &&
                      readback->submission < clusterPlan->steps.size() &&
                      Is<SubmitMaterialSampleStep>(
                          clusterPlan->steps[readback->submission].kind);
      }
    Check(readsSample, "cluster analysis reads its material sample through a "
                       "readback of a sample submission");
  }
  Recipe hiddenRecipe;
  hiddenRecipe.masks = {{"bad", "@missing"}};
  SurfaceOutput hiddenSurface;
  Layer hiddenLayer;
  hiddenLayer.source = Ref{"bad"};
  hiddenSurface.stack.push_back(hiddenLayer);
  hiddenRecipe.outputs.push_back(hiddenSurface);
  const auto hiddenGraph = RecipeGraph::Compile(hiddenRecipe);
  const std::array hiddenRequests{RenderStackRequest{
      &hiddenGraph, &hiddenSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  const auto hiddenPlan = BuildRenderPlan({}, hiddenRequests, Bindings());
  Check(hiddenPlan.has_value(),
        "invalid branch has a typed unavailable producer instead of rejecting "
        "the geometry plan");
  Recipe uniformRecipe;
  uniformRecipe.masks = {{"uniform", "0.7"}};
  SurfaceOutput uniformSurface;
  Layer uniformLayer;
  uniformLayer.source = Ref{"uniform"};
  uniformLayer.mask = Ref{"uniform"};
  uniformSurface.stack.push_back(uniformLayer);
  uniformRecipe.outputs.push_back(uniformSurface);
  const auto uniformGraph = RecipeGraph::Compile(uniformRecipe);
  const std::array uniformRequests{RenderStackRequest{
      &uniformGraph, &uniformSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  Check(BuildRenderPlan({}, uniformRequests, Bindings()).has_value(),
        "uniform scalar masks materialize as fields for stack consumers");
  Recipe rippleRecipe;
  TriggerSignal trigger;
  trigger.origin = EventOrigin{"hit"};
  rippleRecipe.signals = {{"hit", trigger}};
  RippleSource ripple;
  ripple.trigger = Ref{"hit"};
  auto secondRipple = ripple;
  secondRipple.speed = 50.0f;
  rippleRecipe.sources = {{"one", ripple}, {"two", secondRipple}};
  SurfaceOutput rippleSurface;
  Layer one;
  one.source = Ref{"one"};
  Layer two;
  two.source = Ref{"two"};
  rippleSurface.stack = {one, two};
  rippleRecipe.outputs.push_back(rippleSurface);
  const auto rippleGraph = RecipeGraph::Compile(rippleRecipe);
  const std::array rippleRequests{RenderStackRequest{
      &rippleGraph, &rippleSurface, 1, PlacementId{0}, 0, {TextureSize{64}}}};
  const auto ripplePlan = BuildRenderPlan({}, rippleRequests, Bindings());
  Check(ripplePlan.has_value(), "ripple prerequisites lower explicitly");
  if (ripplePlan) {
    std::size_t positions = 0, ripples = 0;
    for (const auto &step : ripplePlan->steps) {
      positions += Is<BakeMeshStep>(step.kind);
      ripples += Is<DrawRippleStep>(step.kind);
    }
    Check(positions == 1 && ripples == 2, "two ripples share a position bake");
  }
  Recipe tintedRecipe;
  SurfaceOutput tintedSurface;
  Layer tinted;
  tinted.source = Vec3{0.5f, 0.5f, 0.5f};
  tinted.color = std::array<Param, 3>{0.5f, 0.5f, 0.5f};
  tinted.curve = CurveRef{"x*x"};
  tintedSurface.stack.push_back(tinted);
  tintedRecipe.outputs.push_back(tintedSurface);
  const auto tintedGraph = RecipeGraph::Compile(tintedRecipe);
  SignalState tintedSignals{tintedGraph};
  tintedSignals.Tick(NullEnvironment{}, {});
  bool checkedTint = false;
  for (const auto &binding : tintedGraph.OutputBindings())
    if (binding.property == LayerWhere(0, 0) + " source") {
      const auto value = AsVec3(tintedSignals.ValueOf(binding.value));
      Check(test::Near(value.x, 0.0625f) && test::Near(value.y, 0.0625f) &&
                test::Near(value.z, 0.0625f),
            "layer tint precedes component curve application");
      checkedTint = true;
    }
  Check(checkedTint, "tinted source keeps its output binding");
  std::vector<TextureDemand> demands;
  const TextureUse use{PlacementId{0}, 0, 0, TextureUseInput::kSource};
  for (const auto &binding : graph.OutputBindings())
    if (binding.property == LayerWhere(0, 0) + " source") {
      const TextureValue value{&graph, binding.value, 1};
      Check(CollectTextureDemand(demands, {value, {TextureSize{64}}, use},
                                 Bindings()(value, GeometryId{}))
                .has_value(),
            "collect mapped arguments and measured field prerequisites");
    }
  const auto demandedPlan = BuildRenderPlan(demands, requests, Bindings());
  Check(demandedPlan && demandedPlan->textureUses.size() == 1,
        "a consumer binding selects its root result rather than each "
        "propagated prerequisite");
  return test::Finish("render plan lowering");
}
