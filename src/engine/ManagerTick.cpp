// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "actors/Lifecycle.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/Clock.h"
#include "engine/LogOnce.h"
#include "engine/RegressionRun.h"
#include "planners/RenderMemory.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "studio/ResolveOutput.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
inline constexpr std::size_t kFirstRendersPerTick = 1;
inline constexpr std::size_t kLevelChangesPerTick = 2;

class SectionTimer {
public:
  explicit SectionTimer(Metrics::Section a_section) noexcept
      : section_(a_section) {}
  ~SectionTimer() { Metrics::CountSection(section_, watch_.Micros()); }
  SectionTimer(const SectionTimer &) = delete;
  SectionTimer &operator=(const SectionTimer &) = delete;
  SectionTimer(SectionTimer &&) = delete;
  SectionTimer &operator=(SectionTimer &&) = delete;

private:
  Metrics::Section section_;
  Metrics::Stopwatch watch_;
};

inline constexpr float kViewRadiusScale = 1.25f;
inline constexpr float kViewRadiusPadding = 64.0f;
inline constexpr float kViewMinRadius = 96.0f;
inline constexpr float kViewCentreHeight = 64.0f;

bool InCameraView(RE::Actor &a_actor) {
  RE::NiCamera *camera = RE::Main::WorldRootCamera();
  RE::NiAVObject *root = a_actor.Get3D(false);
  if (!camera || !root) {
    return true;
  }
  const RE::NiPoint3 position = a_actor.GetPosition();
  const float radius = std::max(root->worldBound.radius, kViewMinRadius);
  const RE::NiBound padded{
      RE::NiPoint3{position.x, position.y, position.z + kViewCentreHeight},
      radius * kViewRadiusScale + kViewRadiusPadding};
  return RE::NiCamera::BoundInFrustum(padded, camera);
}

bool ShownThisTick(RE::Actor &a_actor, const Settings &a_settings,
                   bool a_inspected) {
  if (a_actor.IsPlayerRef() || !a_settings.cullOutOfView) {
    return true;
  }
  const SectionTimer cullTimer{Metrics::Section::kCull};
  const bool inView = InCameraView(a_actor);
  Metrics::CountActorView(inView);
  return inView || a_inspected;
}

bool TakenByAnotherSystem(const LiveGeometry &a_bound) {
  if (a_bound.lost) {
    return false;
  }
  return (a_bound.material && !a_bound.material->StillOwned()) ||
         (a_bound.shell && !a_bound.shell->StillOwned());
}

bool RangeChanged(RE::FormID a_actor, const Actors::ActorState &a_state,
                  float a_evictDistance) {
  const bool installed = !(a_state.installed == Actors::ActorPlan{});
  if (!installed && a_state.plan == Actors::ActorPlan{}) {
    return false;
  }
  const auto *actor = RE::TESForm::LookupByID<RE::Actor>(a_actor);
  if (!actor) {
    return false;
  }
  const std::optional<float> distance = DistanceToPlayer(*actor);
  return distance &&
         Actors::InRange(installed, distance, a_evictDistance) != installed;
}

bool AwaitsFirstRender(const LiveActor &a_state) {
  if (a_state.firstRenderAdmitted) {
    return false;
  }
  return std::ranges::any_of(
      a_state.placements, [](const LivePlacement &a_placement) {
        return std::ranges::any_of(a_placement.outputs,
                                   [](const PlacedOutput &a_output) {
                                     return a_output.stack != nullptr;
                                   });
      });
}

bool AdmitRender(LiveActor &a_state, std::size_t &a_firstRenders) {
  if (!AwaitsFirstRender(a_state)) {
    return true;
  }
  if (a_firstRenders >= kFirstRendersPerTick) {
    return false;
  }
  ++a_firstRenders;
  a_state.firstRenderAdmitted = true;
  return true;
}

struct SlotWrite {
  Slot slot = Slot::kEmissive;
  TextureRef texture;
  bool shown = false;
  std::array<float, kScalarFieldCount> scalars{};
  Vec3 color{1.0f, 1.0f, 1.0f};

  [[nodiscard]] float Of(ScalarField a_field) const {
    return scalars[IndexOf(a_field)];
  }
  [[nodiscard]] float Shown(ScalarField a_field) const {
    return shown ? Of(a_field) : 0.0f;
  }
};

SlotWrite EmptyWrite(Slot a_slot) {
  SlotWrite write;
  write.slot = a_slot;
  for (const ScalarField field : ScalarsOf(a_slot)) {
    write.scalars[IndexOf(field)] = ScalarFallback(field);
  }
  const float fallback = ScalarFallback(ScalarField::kColor);
  write.color = Vec3{fallback, fallback, fallback};
  return write;
}

void WriteSlot(SlotTarget &a_target, const SlotWrite &a_write) {
  a_target.WriteTexture(a_write.slot,
                        a_write.shown ? a_write.texture : nullptr);
  switch (a_write.slot) {
  case Slot::kEmissive:
    a_target.WriteEmissive(Vec3{1.0f, 1.0f, 1.0f},
                           a_write.Shown(ScalarField::kStrength));
    break;
  case Slot::kFuzz:
    a_target.WriteFuzz(a_write.color, a_write.Shown(ScalarField::kWeight));
    break;
  case Slot::kHeight:
    a_target.WriteHeightScale(a_write.Shown(ScalarField::kScale));
    break;
  case Slot::kGlint:
    a_target.WriteGlint(
        {.enabled = a_write.shown,
         .screenSpaceScale = a_write.Of(ScalarField::kScreenSpaceScale),
         .logMicrofacetDensity = a_write.Of(ScalarField::kLogMicrofacetDensity),
         .microfacetRoughness = a_write.Of(ScalarField::kMicrofacetRoughness),
         .densityRandomization =
             a_write.Of(ScalarField::kDensityRandomization)});
    break;
  case Slot::kCoat:
    a_target.WriteCoat(a_write.Of(ScalarField::kRoughness),
                       a_write.Shown(ScalarField::kLevel));
    break;
  case Slot::kSubsurface:
    a_target.WriteSubsurface(a_write.color,
                             a_write.Shown(ScalarField::kThickness));
    break;
  default:
    break;
  }
}

struct TickFrame {
  const Settings &settings;
  const Studio::View &view;
  std::uint32_t nowMS;
  bool resuming;
};

struct SlotRenderView {
  const Studio::View &view;
  bool anyLayerHidden = false;
  bool publish = false;
  bool mergeChains = false;
};

struct BoundMeshes {
  std::vector<RE::BSGeometry *> geometries;
  std::vector<Compositor::MaterialKey> materials;
};

LayerFilter HiddenLayers(const SlotRenderView &a_view,
                         const std::string &a_recipe, std::size_t a_output,
                         std::size_t a_layerCount) {
  LayerFilter filter;
  if (!a_view.anyLayerHidden ||
      !a_view.view.FiltersLayers(a_recipe, a_output)) {
    return filter;
  }
  for (std::size_t i = 0; i < a_layerCount; ++i) {
    if (!a_view.view.LayerShown(a_recipe, a_output, i)) {
      filter.hidden.push_back(i);
    }
  }
  return filter;
}

struct SlotChain {
  std::vector<const SurfaceOutput *> outputs;
  std::vector<Studio::ResolvedOutput> resolved;
  TextureRef texture;
  bool shown = false;
};

void UpdateRenderInputs(LiveActor &a_state, LiveGeometry &a_bound,
                        WarningHistory &a_warnings) {
  if (!a_bound.inputs.render) {
    return;
  }
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    const auto &instance = a_state.instances[i];
    if (!instance.graph || !instance.signals)
      continue;
    if (auto updated = a_bound.inputs.render->Update(*instance.graph, i + 1,
                                                     *instance.signals);
        !updated)
      LogOnce(a_warnings, Severity::kError,
              std::format("render inputs of '{}': {}", a_bound.name,
                          updated.error()));
  }
}

StackRender RenderStackOutput(PlacedOutput &a_output,
                              const LayerFilter &a_filter,
                              const StackBase &a_base) {
  if (!a_output.stack) {
    return StackRender::kFailed;
  }
  const StackRender outcome =
      Compositor::GetSingleton()->Render(*a_output.stack, a_filter, a_base);
  a_output.rendered = outcome == StackRender::kRendered;
  a_output.renderFailed = outcome == StackRender::kFailed;
  return outcome;
}

const MergedSlotStack *MergedSlotOf(const LiveGeometry &a_bound,
                                    const SlotStackPlan &a_slot) {
  for (const MergedSlotStack &merged : a_bound.mergedSlots) {
    if (merged.surface == a_slot.surface && merged.slot == a_slot.slot &&
        merged.stack && merged.layerStarts.size() == merged.links.size()) {
      return &merged;
    }
  }
  return nullptr;
}

std::optional<std::size_t> LinkIndexOf(const MergedSlotStack &a_merged,
                                       ChainLink a_link) {
  const auto found = std::ranges::find(a_merged.links, a_link);
  return found == a_merged.links.end()
             ? std::nullopt
             : std::optional{static_cast<std::size_t>(
                   std::distance(a_merged.links.begin(), found))};
}

std::size_t LinkLayerEnd(const MergedSlotStack &a_merged, std::size_t a_link) {
  return a_link + 1 < a_merged.layerStarts.size()
             ? a_merged.layerStarts[a_link + 1]
             : a_merged.layerCount;
}

void HideLink(LayerFilter &a_filter, const MergedSlotStack &a_merged,
              std::size_t a_link) {
  for (std::size_t layer = a_merged.layerStarts[a_link];
       layer < LinkLayerEnd(a_merged, a_link); ++layer) {
    a_filter.hidden.push_back(layer);
  }
}

struct ShownLink {
  std::size_t index = 0;
  const SurfaceOutput *material = nullptr;
  PlacedOutput *output = nullptr;
  LiveInstance *instance = nullptr;
  std::size_t outputIndex = 0;
};

struct SlotContext {
  LiveActor &state;
  const LiveGeometry &bound;
  const SlotRenderView &view;
};

std::optional<ShownLink> ShownLinkOf(const SlotContext &a_context,
                                     const MergedSlotStack &a_merged,
                                     const StackLink &a_link) {
  const SlotContribution c = a_link.contribution;
  const std::optional<ResolvedPlacement> resolved =
      ResolvePlacement(a_context.state, a_context.bound, IndexOf(c.placed));
  if (!resolved) {
    return std::nullopt;
  }
  LiveInstance &instance = a_context.state.instances[resolved->instance];
  if (!instance.recipe || !instance.signals) {
    return std::nullopt;
  }
  const SurfaceOutput *material = SurfaceOutputOf(*instance.recipe, c.output);
  PlacedOutput *output =
      OutputAt(a_context.state.placements[resolved->placement], c.output);
  const std::optional<std::size_t> index =
      LinkIndexOf(a_merged, {PlacementId{resolved->placement}, c.output});
  if (!material || !output || !index ||
      !a_context.view.view.OutputShown(instance.recipe->id, c.output)) {
    return std::nullopt;
  }
  return ShownLink{*index, material, output, &instance, c.output};
}

SlotChain RenderMergedChain(const SlotContext &a_context,
                            const SlotStackPlan &a_slot,
                            const MergedSlotStack &a_merged) {
  SlotChain chain;
  LayerFilter filter;
  std::vector<bool> shown(a_merged.links.size(), false);
  std::vector<PlacedOutput *> drawn;
  for (const StackLink &link : a_slot.chain) {
    const std::optional<ShownLink> found =
        ShownLinkOf(a_context, a_merged, link);
    if (!found) {
      continue;
    }
    shown[found->index] = true;
    const LayerFilter own =
        HiddenLayers(a_context.view, found->instance->recipe->id,
                     found->outputIndex, found->material->stack.size());
    for (const std::size_t layer : own.hidden) {
      filter.hidden.push_back(a_merged.layerStarts[found->index] + layer);
    }
    chain.outputs.push_back(found->material);
    chain.resolved.push_back(
        Studio::ResolveOutput(*found->material, *found->instance->signals));
    drawn.push_back(found->output);
  }
  for (std::size_t link = 0; link < shown.size(); ++link) {
    if (!shown[link]) {
      HideLink(filter, a_merged, link);
    }
  }
  if (drawn.empty()) {
    return chain;
  }
  const StackRender outcome =
      Compositor::GetSingleton()->Render(*a_merged.stack, filter, StackBase{});
  for (PlacedOutput *output : drawn) {
    output->rendered = outcome == StackRender::kRendered;
    output->renderFailed = outcome == StackRender::kFailed;
  }
  if (outcome != StackRender::kRendered) {
    return SlotChain{};
  }
  chain.shown = true;
  chain.texture = a_merged.stack->Texture();
  return chain;
}

SlotChain RenderSlotChain(LiveActor &a_state, LiveGeometry &a_bound,
                          const SlotRenderView &a_view,
                          const SlotStackPlan &a_slot) {
  if (const MergedSlotStack *merged =
          a_view.mergeChains ? MergedSlotOf(a_bound, a_slot) : nullptr) {
    return RenderMergedChain({a_state, a_bound, a_view}, a_slot, *merged);
  }
  SlotChain chain;
  StackBase base;
  for (const StackLink &link : a_slot.chain) {
    const SlotContribution c = link.contribution;
    const std::optional<ResolvedPlacement> resolved =
        ResolvePlacement(a_state, a_bound, IndexOf(c.placed));
    if (!resolved) {
      continue;
    }
    LiveInstance &instance = a_state.instances[resolved->instance];
    if (!instance.recipe || !instance.signals) {
      continue;
    }
    const SurfaceOutput *material = SurfaceOutputOf(*instance.recipe, c.output);
    PlacedOutput *output =
        OutputAt(a_state.placements[resolved->placement], c.output);
    if (!material || !output || !output->stack ||
        !a_view.view.OutputShown(instance.recipe->id, c.output)) {
      continue;
    }
    const LayerFilter filter = HiddenLayers(a_view, instance.recipe->id,
                                            c.output, material->stack.size());
    if (RenderStackOutput(*output, filter, base) != StackRender::kRendered) {
      continue;
    }
    chain.shown = true;
    if (TextureRef texture = output->stack->Texture()) {
      base = StackBase{texture, output->stack->ContentVersion()};
      chain.texture = texture;
    }
    chain.outputs.push_back(material);
    chain.resolved.push_back(
        Studio::ResolveOutput(*material, *instance.signals));
  }
  return chain;
}

void ApplySlotScalars(SlotWrite &a_write, Slot a_slot,
                      const SlotChain &a_chain) {
  for (const ScalarField field : ScalarsOf(a_slot)) {
    const std::optional<std::size_t> at =
        ScalarSource(a_slot, field, a_chain.outputs);
    if (!at) {
      continue;
    }
    if (field == ScalarField::kColor) {
      a_write.color = a_chain.resolved[*at].color;
    } else {
      a_write.scalars[IndexOf(field)] = a_chain.resolved[*at].Scalar(field);
    }
  }
}

void PoseShell(LiveGeometry &a_bound, LiveActor &a_state,
               const Studio::View &a_view) {
  if (!a_bound.shell || !a_bound.shellOwner ||
      *a_bound.shellOwner >= a_state.instances.size()) {
    return;
  }
  LiveInstance &instance = a_state.instances[*a_bound.shellOwner];
  if (!instance.recipe || !instance.signals) {
    return;
  }
  const ShellSettings &shell = instance.recipe->shell;
  SignalState &signals = *instance.signals;
  const ShellPoseValues pose{
      .inflate = signals.Resolve(shell.pose.inflate),
      .offset = signals.Resolve(shell.pose.offset),
      .scale = signals.Resolve(shell.pose.scale),
      .scalePoint = shell.pose.scalePoint,
      .spin = signals.Resolve(shell.pose.spin),
      .spinAxis = shell.pose.spinAxis,
  };
  a_bound.shell->Pose(pose, signals.Resolve(shell.opacity),
                      signals.Resolve(shell.rimPower),
                      signals.Resolve(shell.emissive));
  a_bound.shell->SetVisible(a_view.RecipeShown(instance.recipe->id));
}

struct InstanceTiming {
  float time = 0.0f;
  float delta = 0.0f;
};

InstanceTiming InstanceTimeFor(LiveInstance &a_instance,
                               const TickFrame &a_frame) {
  if (!a_instance.recipe) {
    return InstanceTiming{a_instance.lastTime, 0.0f};
  }
  const Studio::View &view = a_frame.view;
  const float speed = InstanceSpeed(a_frame.settings.animationSpeed, view.speed,
                                    a_instance.recipe->clock.speed);
  if (a_frame.resuming) {
    a_instance.startMS =
        a_frame.nowMS - ClockOffsetMS(view.scrubSeconds, speed).value_or(0);
  }
  const float time =
      view.freeze ? view.scrubSeconds
                  : static_cast<float>(a_frame.nowMS - a_instance.startMS) *
                        0.001f * speed;
  const float delta = std::max(0.0f, time - a_instance.lastTime);
  return InstanceTiming{time, delta};
}

void AddBoundMeshes(BoundMeshes &a_meshes, const LivePiece &a_piece) {
  for (const LiveGeometry &g : a_piece.geometries) {
    if (!g.lost) {
      a_meshes.geometries.push_back(g.geometry.get());
      a_meshes.materials.emplace_back(g.inputs.material.rmaos.get(),
                                      g.inputs.material.diffuse.get());
    }
  }
}

BoundMeshes
BoundMeshesOf(const std::unordered_map<RE::FormID, LiveActor> &a_applied) {
  BoundMeshes meshes;
  for (const auto &[actorID, state] : a_applied) {
    for (const LivePiece &piece : state.pieces) {
      AddBoundMeshes(meshes, piece);
    }
  }
  return meshes;
}

void SweepBoundMeshes(
    Compositor &a_compositor,
    const std::unordered_map<RE::FormID, LiveActor> &a_applied,
    std::uint32_t a_nowMS) {
  if (!a_compositor.MeshSweepDue(a_nowMS)) {
    return;
  }
  const BoundMeshes meshes = BoundMeshesOf(a_applied);
  a_compositor.SweepMeshes(a_nowMS, meshes.geometries);
  a_compositor.SweepMaterials(a_nowMS, meshes.materials);
}

void FailGeometryOutputs(LiveActor &a_state, const LiveGeometry &a_bound) {
  for (const PlacementId id : a_bound.placements) {
    if (const std::optional<ResolvedPlacement> resolved =
            ResolvePlacement(a_state, id)) {
      for (PlacedOutput &output :
           a_state.placements[resolved->placement].outputs) {
        output.renderFailed = true;
      }
    }
  }
}

void WriteSlots(LiveActor &a_state, LiveGeometry &a_bound,
                const SlotRenderView &a_view, bool a_hidden) {
  for (const SlotStackPlan &slot : a_bound.stackPlan.slots) {
    SlotTarget *target = TargetFor(a_bound, slot.surface);
    if (!target) {
      continue;
    }
    SlotWrite write = EmptyWrite(slot.slot);
    if (!a_hidden) {
      const SlotChain chain = RenderSlotChain(a_state, a_bound, a_view, slot);
      write.shown = chain.shown && a_view.publish;
      write.texture = chain.texture;
      ApplySlotScalars(write, slot.slot, chain);
    }
    WriteSlot(*target, write);
  }
}

void MarkReferencedInstances(LiveActor &a_state, const LiveGeometry &a_bound,
                             std::vector<bool> &a_referenced) {
  for (const PlacementId id : a_bound.placements) {
    if (const std::optional<ResolvedPlacement> resolved =
            ResolvePlacement(a_state, id)) {
      a_referenced[resolved->instance] = true;
    }
  }
}

void EmitGpuTimings() {
  const GpuTiming::Totals gpu = TextureLab::GetSingleton()->DrainTimings();
  if (gpu.timedTicks == 0 && gpu.droppedTicks == 0 && gpu.discardedTicks == 0)
    return;
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "gpu_ticks"},
                     {"timed", std::to_string(gpu.timedTicks)},
                     {"dropped", std::to_string(gpu.droppedTicks)},
                     {"discarded", std::to_string(gpu.discardedTicks)},
                     {"untimed_spans", std::to_string(gpu.untimedSpans)}});
  for (const auto &[span, total] : gpu.spans)
    Trace::EmitSafely(
        Trace::Event::kMetrics,
        {{"action", "gpu_span"},
         {"span", span},
         {"count", std::to_string(total.count)},
         {"total_us", std::to_string(total.nanoseconds / 1000)},
         {"max_us", std::to_string(total.maxNanoseconds / 1000)}});
}

void EmitStepKinds(const Metrics::StepKindCounts &a_counts) {
  std::vector<Trace::Field> fields{{"action", "step_kinds"}};
  for (std::size_t kind = 0; kind < kRenderStepKindCount; ++kind) {
    if (a_counts[kind] > 0) {
      fields.push_back(
          {std::string{StepKindNameAt(kind)}, std::to_string(a_counts[kind])});
    }
  }
  if (fields.size() > 1) {
    Trace::EmitSafely(Trace::Event::kMetrics, fields);
  }
}

std::vector<const RenderInstance *> InstancesOf(const LiveActor &a_state) {
  std::vector<const RenderInstance *> instances;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render &&
          !std::ranges::contains(instances, geometry.inputs.render.get())) {
        instances.push_back(geometry.inputs.render.get());
      }
    }
  }
  return instances;
}

std::vector<const RenderInstance *>
RenderedInstances(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::unordered_set<const RenderInstance *> seen;
  std::vector<const RenderInstance *> instances;
  for (const auto &[actor, state] : a_live) {
    if (state.renderedFrame == 0) {
      continue;
    }
    for (const RenderInstance *render : InstancesOf(state)) {
      if (seen.insert(render).second) {
        instances.push_back(render);
      }
    }
  }
  return instances;
}

TargetCost
EstimateTargets(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::vector<HeldTarget> held;
  for (const RenderInstance *render : RenderedInstances(a_live)) {
    const PlanTargets &targets = render->Targets();
    held.insert(held.end(), targets.held.begin(), targets.held.end());
  }
  return CostOf(held);
}

ScreenSpread
ScreenSpreadOf(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::vector<float> fractions;
  for (const auto &[actor, state] : a_live) {
    if (state.inView && state.renderedFrame != 0) {
      fractions.push_back(state.screenFraction);
    }
  }
  return SpreadOf(std::move(fractions));
}

std::vector<std::string> RecipeIdsOf(const LiveActor &a_state) {
  std::vector<std::string> ids;
  for (const LiveInstance &instance : a_state.instances) {
    if (instance.recipe && !std::ranges::contains(ids, instance.recipe->id)) {
      ids.push_back(instance.recipe->id);
    }
  }
  return ids;
}

ActorTargets TargetsOfActor(RE::FormID a_actor, const LiveActor &a_state) {
  std::vector<HeldTarget> held;
  for (const RenderInstance *render : InstancesOf(a_state)) {
    const PlanTargets &targets = render->Targets();
    held.insert(held.end(), targets.held.begin(), targets.held.end());
  }
  const RE::NiPointer<RE::Actor> actor = a_state.actor.get();
  const char *name = actor ? actor->GetName() : nullptr;
  return {a_actor, name ? std::string{name} : std::string{},
          a_state.screenFraction, RecipeIdsOf(a_state), std::move(held)};
}

std::vector<ActorTargets>
RenderedActorTargets(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::vector<ActorTargets> actors;
  for (const auto &[actor, state] : a_live) {
    if (state.renderedFrame != 0) {
      actors.push_back(TargetsOfActor(actor, state));
    }
  }
  return actors;
}

std::string Fraction(float a_value) { return std::format("{:.3f}", a_value); }

void EmitLevelCounts(const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  std::array<std::size_t, kResolutionLevelCount> counts{};
  for (const auto &[actor, state] : a_live) {
    const auto index = static_cast<std::size_t>(state.resolution);
    if (index < counts.size()) {
      ++counts[index];
    }
  }
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "resolution_levels"},
                     {"full", std::to_string(counts[0])},
                     {"half", std::to_string(counts[1])},
                     {"quarter", std::to_string(counts[2])},
                     {"eighth", std::to_string(counts[3])}});
}

void EmitMetricsHeartbeat(const Metrics::Snapshot &a_measured,
                          const TargetCost &a_estimate,
                          const ScreenSpread &a_screen) {
  EmitGpuTimings();
  const TextureLab::TargetHoldings held =
      TextureLab::GetSingleton()->HeldTargets();
  const Compositor *compositor = Compositor::GetSingleton();
  EmitStepKinds(a_measured.stepExecutionsByKind);
  std::vector<Trace::Field> heartbeat{
      {"action", "heartbeat"},
      {"syncs", std::to_string(a_measured.syncs)},
      {"sync_us", std::to_string(a_measured.syncMicros)},
      {"sync_max_us", std::to_string(a_measured.syncMaxMicros)},
      {"sink_adds", std::to_string(a_measured.sinkAdds)},
      {"sink_removes", std::to_string(a_measured.sinkRemoves)},
      {"readbacks", std::to_string(a_measured.readbacks)},
      {"readback_us", std::to_string(a_measured.readbackMicros)},
      {"readback_max_us", std::to_string(a_measured.readbackMaxMicros)},
      {"targets", std::to_string(a_measured.targets)},
      {"targets_peak", std::to_string(a_measured.targetsPeak)},
      {"target_bytes", std::to_string(a_measured.targetBytes)},
      {"target_bytes_peak", std::to_string(a_measured.targetBytesPeak)},
      {"targets_created", std::to_string(a_measured.targetsCreated)},
      {"target_bytes_created", std::to_string(a_measured.targetBytesCreated)},
      {"target_bytes_destroyed",
       std::to_string(a_measured.targetBytesDestroyed)},
      {"screen_fraction_min", Fraction(a_screen.least)},
      {"screen_fraction_median", Fraction(a_screen.median)},
      {"screen_fraction_max", Fraction(a_screen.most)},
      {"estimated_targets", std::to_string(a_estimate.targets)},
      {"estimated_bytes", std::to_string(a_estimate.bytes)},
      {"pool_targets", std::to_string(held.idleTargets)},
      {"pool_bytes", std::to_string(held.idleBytes)},
      {"scratch_targets", std::to_string(held.scratchTargets)},
      {"scratch_bytes", std::to_string(held.scratchBytes)},
      {"resident_targets", std::to_string(compositor->ResidentTargets())},
      {"resident_bytes", std::to_string(compositor->ResidentBytes())},
      {"frames", std::to_string(a_measured.frames)},
      {"render_evaluations", std::to_string(a_measured.renderEvaluations)},
      {"step_executions", std::to_string(a_measured.stepExecutions)},
      {"step_cache_hits", std::to_string(a_measured.stepCacheHits)},
      {"step_releases", std::to_string(a_measured.stepReleases)},
      {"step_restores", std::to_string(a_measured.stepRestores)},
      {"interval_max_us", std::to_string(a_measured.intervalMaxMicros)},
      {"actors_in_view", std::to_string(a_measured.actorsInView)},
      {"actors_out_of_view", std::to_string(a_measured.actorsOutOfView)},
      {"hitches_50ms", std::to_string(a_measured.hitches[0])},
      {"hitches_100ms", std::to_string(a_measured.hitches[1])},
      {"plugin_over_8ms", std::to_string(a_measured.pluginSpikes[0])},
      {"plugin_over_16ms", std::to_string(a_measured.pluginSpikes[1])},
      {"plugin_over_33ms", std::to_string(a_measured.pluginSpikes[2])}};
  for (const Named<Metrics::Section> &section : Metrics::kSections) {
    const Metrics::SectionTime time = a_measured.Of(section.value);
    heartbeat.push_back(
        {std::format("{}_us", section.name), std::to_string(time.micros)});
    heartbeat.push_back({std::format("{}_max_us", section.name),
                         std::to_string(time.maxMicros)});
  }
  Trace::EmitSafely(Trace::Event::kMetrics, heartbeat);
}
}

void Manager::FlushMetricsHeartbeat() {
  const std::uint32_t now = NowMS();
  const std::uint32_t intervalMs = now - lastMetricsMS_;
  lastMetricsMS_ = now;
  const Metrics::Snapshot measured = Metrics::Drain();
  const TargetCost planned = EstimateTargets(live_);
  EmitMetricsHeartbeat(measured, planned, ScreenSpreadOf(live_));
  EmitLevelCounts(live_);
  ReportRenderMemory(measured, planned, intervalMs);
}

void Manager::ReportRenderMemory(const Metrics::Snapshot &a_measured,
                                 const TargetCost &a_planned,
                                 std::uint32_t a_intervalMs) {
  const HoldingLevel next =
      NextHoldingLevel(holdingLevel_, a_measured.targetBytes);
  holdingLevel_ = next.level;
  if (next.crossing != Crossing::kNone) {
    logger::info("{}", HoldingLine({a_measured.targets, a_measured.targetBytes},
                                   {a_planned.targets, a_planned.bytes},
                                   LargestHoldings(
                                       HoldingsOf(RenderedActorTargets(live_)),
                                       kReportedHoldings)));
  }
  const TargetBurst burst{a_measured.targetsCreated,
                          a_measured.targetBytesCreated,
                          a_measured.targetBytesDestroyed, a_intervalMs};
  if (IsBurst(burst) && GetSettings().verboseLogging) {
    logger::info("{}",
                 BurstLine(burst, Actors::SyncSummary(syncLog_, NowMS(),
                                                      Actors::kSyncLogMs)));
  }
}

void Manager::OnFrame() {
  if (!textureLabStarted_) {
    textureLabStarted_ = true;
    const Metrics::Stopwatch startWatch;
    const bool started = TextureLab::GetSingleton()->Init();
    Trace::EmitSafely(Trace::Event::kMetrics,
                      {{"action", "texture_lab_start"},
                       {"started", started ? "true" : "false"},
                       {"us", std::to_string(startWatch.Micros())}});
  }
  if (applications_.Loading()) {
    Metrics::ForgetFrameInterval();
    return;
  }
  AdvanceRegressionRun();
  const std::uint32_t now = NowMS();
  if (now - lastMetricsMS_ >= 1000) {
    FlushMetricsHeartbeat();
  }
  const SectionTimer frameTimer{Metrics::Section::kFrame};
  TextureLab::GetSingleton()->CollectTimings();
  SweepRetiredMaterialTextures();
  TextureLab::GetSingleton()->CollectPreviewDraws();
  FireDueFinalizes();
  editor_.TickGesture();
  Metrics::CountFrame();
  ++renderFrame_;
  if (now - lastSweepMS_ >= 1000) {
    lastSweepMS_ = now;
    carriedTimes_.Expire(now);
    SweepAnimationEvents();
  }
  const Settings settings = GetSettings();
  if (now - lastWaitingCheckMS_ >= Actors::kPendingRecheckMs) {
    lastWaitingCheckMS_ = now;
    MarkWaitingStale(now);
  }
  if (now - lastRecheckMS_ >= Actors::kRecheckMs) {
    lastRecheckMS_ = now;
    MarkRecheckStale(now, settings.evictDistance);
  }
  if (now - lastTickMS_ < settings.TickIntervalMS()) {
    return;
  }
  lastTickMS_ = now;
  {
    const SectionTimer sweepTimer{Metrics::Section::kSweep};
    Compositor::GetSingleton()->BeginTick(now);
    Compositor::GetSingleton()->SweepStepOutputs(now);
  }
  if (!live_.empty()) {
    const SectionTimer tickTimer{Metrics::Section::kTick};
    TextureLab::GetSingleton()->SetGeneratedShaders(settings.generatedShaders);
    TextureLab::GetSingleton()->BeginTimedTick(settings.gpuTiming);
    Tick(now, settings);
    TextureLab::GetSingleton()->EndTimedTick();
  }
  {
    const SectionTimer sweepTimer{Metrics::Section::kSweep};
    SweepBoundMeshes(*Compositor::GetSingleton(), live_, now);
  }
  {
    const SectionTimer snapshotTimer{Metrics::Section::kSnapshot};
    PublishSnapshot(now);
  }
  ObserveRegression();
}

void Manager::MarkWaitingStale(std::uint32_t a_nowMS) {
  for (const auto &[id, tracked] : tracked_) {
    if (Actors::RecheckOf(tracked.state, a_nowMS) ==
        Actors::Recheck::kWaiting) {
      MarkStale(id);
    }
  }
}

void Manager::MarkRecheckStale(std::uint32_t a_nowMS, float a_evictDistance) {
  for (const auto &[id, tracked] : tracked_) {
    if (Actors::RecheckOf(tracked.state, a_nowMS) != Actors::Recheck::kNone ||
        (tracked.state && RangeChanged(id, *tracked.state, a_evictDistance))) {
      MarkStale(id);
    }
  }
}

void Manager::FireDueFinalizes() { applications_.FinalizeDue(NowMS()); }

void Manager::Tick(std::uint32_t a_nowMS, const Settings &a_settings) {
  tickNowMS_ = a_nowMS;
  const Studio::View &view = editor_.CurrentView();
  {
    const SectionTimer previewsTimer{Metrics::Section::kPreviews};
    TextureLab::GetSingleton()->RenderPreviews();
  }
  const TickFrame frame{a_settings, view, a_nowMS,
                        frozenLastTick_ && !view.freeze};
  frozenLastTick_ = view.freeze;
  const std::optional<RE::FormID> inspected = InspectedActor(a_nowMS);
  std::size_t firstRenders = 0;
  std::size_t levelChanges = 0;
  for (auto it = live_.begin(); it != live_.end();) {
    LiveActor &state = it->second;
    const auto actor = state.actor.get();
    if (!actor || !Loaded3D(*actor)) {
      const RE::FormID id = it->first;
      AbandonApplications(id);
      ++it;
      ForgetActor(id);
      continue;
    }
    ClaimLostGeometries(it->first, state);
    {
      const SectionTimer signalsTimer{Metrics::Section::kSignals};
      for (LiveInstance &instance : state.instances) {
        if (!instance.recipe || !instance.graph || !instance.signals ||
            !instance.environment) {
          continue;
        }
        const InstanceTiming timing = InstanceTimeFor(instance, frame);
        TickInstance(instance, timing.time, timing.delta);
        instance.lastTime = timing.time;
      }
    }
    state.inView = ShownThisTick(*actor, a_settings, inspected == it->first);
    state.screenFraction = ScreenFraction(*actor);
    if (levelChanges < kLevelChangesPerTick &&
        UpdateResolution(it->first, *actor, state.screenFraction, a_settings)) {
      ++levelChanges;
    }
    const bool render = state.inView && AdmitRender(state, firstRenders);
    std::vector<bool> hidden;
    if (render) {
      const SectionTimer renderTimer{Metrics::Section::kRender};
      hidden = RenderPieces(state, it->first);
      state.renderedFrame = renderFrame_;
    }
    if (render || !state.inView) {
      const SectionTimer lightsTimer{Metrics::Section::kLights};
      UpdateLights(state, hidden);
    }
    FinishApplications(it->first, state);
    ++it;
  }
}

std::vector<bool> Manager::RenderPieces(LiveActor &a_state,
                                        RE::FormID a_actorID) {
  const Studio::View &view = editor_.CurrentView();
  const bool inspected = InspectedActor(NowMS()) == a_actorID;
  const bool soloingPiece = view.soloPiece.has_value();
  std::vector<bool> hidden;
  std::vector<bool> shown;
  if (soloingPiece) {
    hidden.assign(a_state.instances.size(), false);
    shown.assign(a_state.instances.size(), false);
  }
  for (LivePiece &piece : a_state.pieces) {
    const bool pieceHidden =
        soloingPiece && !view.PieceShown(a_actorID, piece.armor);
    std::vector<bool> &referenced = pieceHidden ? hidden : shown;
    for (LiveGeometry &bound : piece.geometries) {
      RenderGeometry(a_state, bound, pieceHidden, inspected);
      if (soloingPiece) {
        MarkReferencedInstances(a_state, bound, referenced);
      }
    }
  }
  for (std::size_t i = 0; i < hidden.size(); ++i) {
    if (shown[i]) {
      hidden[i] = false;
    }
  }
  return hidden;
}

void Manager::TickInstance(LiveInstance &a_instance, float a_time,
                           float a_delta) {
  if (!a_instance.graph || !a_instance.signals || !a_instance.environment) {
    return;
  }
  if (editor_.CurrentView().freeze && a_time + 0.001f < a_instance.lastTime) {
    a_instance.signals = std::make_unique<SignalState>(*a_instance.graph);
    a_delta = a_time;
  }
  a_instance.signals->Tick(*a_instance.environment, {a_time, a_delta});
}

bool Manager::UpdateResolution(RE::FormID a_actorID, RE::Actor &a_actor,
                               float a_screenFraction,
                               const Settings &a_settings) {
  const auto tracked = tracked_.find(a_actorID);
  if (tracked == tracked_.end() || !tracked->second.resolution) {
    return false;
  }
  ResolutionLevel &level = *tracked->second.resolution;
  const ActorRole role =
      a_actor.IsPlayerRef() ? ActorRole::kPlayer : ActorRole::kOther;
  const float fraction =
      RegressionScreenResolution().value_or(a_settings.screenResolution)
          ? a_screenFraction
          : 1.0f;
  const std::optional<ResolutionLevel> next =
      LevelChange(level, fraction, role);
  if (!next || !applications_.MarkReinstall(a_actorID)) {
    return false;
  }
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "resolution_level"},
                     {"actor", std::to_string(a_actorID)},
                     {"from", std::string{LevelName(level)}},
                     {"to", std::string{LevelName(*next)}},
                     {"screen_fraction", Fraction(a_screenFraction)}});
  level = *next;
  tracked->second.resizing = true;
  return true;
}

void Manager::ClaimLostGeometries(RE::FormID a_actorID, LiveActor &a_state) {
  std::vector<Actors::Claim> claims;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &bound : piece.geometries) {
      if (TakenByAnotherSystem(bound)) {
        logger::info("claimed '{}': its material or shell was replaced by "
                     "another system",
                     bound.name);
        RetireGeometry(a_state, bound);
        claims.push_back({bound.ref, piece.model});
      }
    }
  }
  if (claims.empty()) {
    return;
  }
  const auto tracked = tracked_.find(a_actorID);
  if (tracked != tracked_.end() && tracked->second.state) {
    for (Actors::Claim &claim : claims) {
      tracked->second.state = Actors::WithClaim(
          std::move(*tracked->second.state), std::move(claim));
    }
  }
  MarkStale(a_actorID);
}

void Manager::RenderGeometry(LiveActor &a_state, LiveGeometry &a_bound,
                             bool a_hidden, bool a_inspected) {
  const TextureLab::PassBatch batch{*TextureLab::GetSingleton()};
  const Studio::View &view = editor_.CurrentView();
  if (a_bound.inputs.render &&
      a_bound.inputs.render->BeginFrame(renderFrame_, tickNowMS_)) {
    const SectionTimer inputsTimer{Metrics::Section::kRenderInputs};
    UpdateRenderInputs(a_state, a_bound, warningHistory_);
  }
  if (a_bound.lost) {
    FailGeometryOutputs(a_state, a_bound);
    return;
  }
  const SlotRenderView slotView{
      view, view.isolation.layer.has_value() || !view.muted.empty(),
      GetSettings().publishEffects, !a_inspected};
  {
    const SectionTimer slotsTimer{Metrics::Section::kRenderSlots};
    WriteSlots(a_state, a_bound, slotView, a_hidden);
  }
  if (a_hidden || !slotView.publish) {
    if (a_bound.shell) {
      a_bound.shell->SetVisible(false);
    }
  } else {
    const SectionTimer shellTimer{Metrics::Section::kRenderShell};
    PoseShell(a_bound, a_state, view);
  }
}

void Manager::UpdateLights(LiveActor &a_state,
                           const std::vector<bool> &a_instanceHidden) {
  const Studio::View &view = editor_.CurrentView();
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    LiveInstance &instance = a_state.instances[i];
    if (!instance.light || !instance.lightOutput || !instance.recipe ||
        !instance.signals) {
      continue;
    }
    const LightOutput *light =
        LightOutputOf(*instance.recipe, *instance.lightOutput);
    if (!light) {
      continue;
    }
    const Studio::ResolvedLight resolved =
        Studio::ResolveLight(*light, *instance.signals);
    const bool hidden = i < a_instanceHidden.size() && a_instanceHidden[i];
    instance.light->Update(
        {resolved.color, resolved.intensity, resolved.size, resolved.cutoff},
        !hidden &&
            view.OutputShown(instance.recipe->id, *instance.lightOutput));
  }
}

}
