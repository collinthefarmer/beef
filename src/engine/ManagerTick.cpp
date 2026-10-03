// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/Clock.h"
#include "engine/RegressionRun.h"
#include "planners/Eviction.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "studio/ResolveOutput.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
inline constexpr std::size_t kFirstRendersPerTick = 1;
inline constexpr std::uint32_t kArmorModelCheckMS = 100;
inline constexpr std::uint32_t kArmorModelWaitMS = 1000;
inline constexpr std::uint32_t kEvictionCheckMS = 1000;

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

bool BeyondEvictionRadius(const RE::Actor &a_actor,
                          const Settings &a_settings) {
  const auto *player = RE::PlayerCharacter::GetSingleton();
  return player && !a_actor.IsPlayerRef() && !a_settings.playerOnly &&
         a_settings.evictDistance > 0.0f &&
         EvictionFor(player->GetPosition().GetDistance(a_actor.GetPosition()),
                     a_settings.evictDistance, true) == EvictionAction::kEvict;
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

void UpdateRenderInputs(LiveActor &a_state, LiveGeometry &a_bound) {
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
      logger::error("render inputs: {}", updated.error());
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

class PhaseTimer {
public:
  explicit PhaseTimer(Metrics::Phase a_phase) noexcept : phase_(a_phase) {}
  ~PhaseTimer() { Metrics::CountPhase(phase_, watch_.Micros()); }
  PhaseTimer(const PhaseTimer &) = delete;
  PhaseTimer &operator=(const PhaseTimer &) = delete;
  PhaseTimer(PhaseTimer &&) = delete;
  PhaseTimer &operator=(PhaseTimer &&) = delete;

private:
  Metrics::Phase phase_;
  Metrics::Stopwatch watch_;
};

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
      fields.push_back({StepKindNameAt(kind), std::to_string(a_counts[kind])});
    }
  }
  if (fields.size() > 1) {
    Trace::EmitSafely(Trace::Event::kMetrics, fields);
  }
}

struct PhaseFieldNames {
  std::string_view total;
  std::string_view longest;
};

inline constexpr std::array<PhaseFieldNames, Metrics::kPhaseCount> kPhaseFields{
    {{"frame_us", "frame_max_us"},
     {"tick_us", "tick_max_us"},
     {"snapshot_us", "snapshot_max_us"},
     {"signals_us", "signals_max_us"},
     {"render_us", "render_max_us"},
     {"lights_us", "lights_max_us"},
     {"previews_us", "previews_max_us"},
     {"sweep_us", "sweep_max_us"}}};

void EmitMetricsHeartbeat() {
  EmitGpuTimings();
  const Metrics::Snapshot measured = Metrics::Drain();
  const TextureLab::TargetHoldings held =
      TextureLab::GetSingleton()->HeldTargets();
  const Compositor *compositor = Compositor::GetSingleton();
  EmitStepKinds(measured.stepExecutionsByKind);
  std::vector<Trace::Field> heartbeat{
      {"action", "heartbeat"},
      {"refreshes", std::to_string(measured.refreshes)},
      {"refresh_us", std::to_string(measured.refreshMicros)},
      {"refresh_max_us", std::to_string(measured.refreshMaxMicros)},
      {"sink_adds", std::to_string(measured.sinkAdds)},
      {"sink_removes", std::to_string(measured.sinkRemoves)},
      {"readbacks", std::to_string(measured.readbacks)},
      {"readback_us", std::to_string(measured.readbackMicros)},
      {"readback_max_us", std::to_string(measured.readbackMaxMicros)},
      {"targets", std::to_string(measured.targets)},
      {"targets_peak", std::to_string(measured.targetsPeak)},
      {"target_bytes", std::to_string(measured.targetBytes)},
      {"target_bytes_peak", std::to_string(measured.targetBytesPeak)},
      {"pool_targets", std::to_string(held.idleTargets)},
      {"pool_bytes", std::to_string(held.idleBytes)},
      {"scratch_targets", std::to_string(held.scratchTargets)},
      {"scratch_bytes", std::to_string(held.scratchBytes)},
      {"resident_targets", std::to_string(compositor->ResidentTargets())},
      {"resident_bytes", std::to_string(compositor->ResidentBytes())},
      {"frames", std::to_string(measured.frames)},
      {"render_evaluations", std::to_string(measured.renderEvaluations)},
      {"step_executions", std::to_string(measured.stepExecutions)},
      {"step_cache_hits", std::to_string(measured.stepCacheHits)},
      {"step_releases", std::to_string(measured.stepReleases)},
      {"step_restores", std::to_string(measured.stepRestores)},
      {"interval_max_us", std::to_string(measured.intervalMaxMicros)},
      {"hitches_50ms", std::to_string(measured.hitches[0])},
      {"hitches_100ms", std::to_string(measured.hitches[1])},
      {"plugin_over_8ms", std::to_string(measured.pluginSpikes[0])},
      {"plugin_over_16ms", std::to_string(measured.pluginSpikes[1])},
      {"plugin_over_33ms", std::to_string(measured.pluginSpikes[2])}};
  for (std::size_t i = 0; i < Metrics::kPhaseCount; ++i) {
    heartbeat.push_back(
        {kPhaseFields[i].total, std::to_string(measured.phases[i].micros)});
    heartbeat.push_back({kPhaseFields[i].longest,
                         std::to_string(measured.phases[i].maxMicros)});
  }
  Trace::EmitSafely(Trace::Event::kMetrics, heartbeat);
}
}

void Manager::FlushMetricsHeartbeat() {
  lastMetricsMS_ = NowMS();
  EmitMetricsHeartbeat();
}

void Manager::OnFrame() {
  if (applications_.Loading()) {
    return;
  }
  AdvanceRegressionRun();
  const PhaseTimer frameTimer{Metrics::Phase::kFrame};
  TextureLab::GetSingleton()->CollectTimings();
  SweepRetiredMaterialTextures();
  TextureLab::GetSingleton()->CollectPreviewDraws();
  FireDueFinalizes();
  editor_.TickGesture();
  Metrics::CountFrame();
  ++renderFrame_;
  const std::uint32_t now = NowMS();
  if (now - lastMetricsMS_ >= 1000) {
    lastMetricsMS_ = now;
    carriedTimes_.Expire(now);
    SweepAnimationEvents();
    EmitMetricsHeartbeat();
  }
  const Settings settings = GetSettings();
  if (now - lastArmorModelCheckMS_ >= kArmorModelCheckMS) {
    lastArmorModelCheckMS_ = now;
    SweepAwaitingArmorModels(settings);
  }
  if (now - lastEvictionMS_ >= kEvictionCheckMS) {
    lastEvictionMS_ = now;
    SweepEviction(settings);
  }
  if (now - lastTickMS_ < settings.TickIntervalMS()) {
    return;
  }
  lastTickMS_ = now;
  {
    const PhaseTimer sweepTimer{Metrics::Phase::kSweep};
    Compositor::GetSingleton()->BeginTick(now);
    Compositor::GetSingleton()->SweepStepOutputs(now);
  }
  if (!applied_.empty()) {
    const PhaseTimer tickTimer{Metrics::Phase::kTick};
    TextureLab::GetSingleton()->SetGeneratedShaders(settings.generatedShaders);
    TextureLab::GetSingleton()->BeginTimedTick(settings.gpuTiming);
    Tick(now, settings);
    TextureLab::GetSingleton()->EndTimedTick();
  }
  {
    const PhaseTimer sweepTimer{Metrics::Phase::kSweep};
    SweepBoundMeshes(*Compositor::GetSingleton(), applied_, now);
  }
  {
    const PhaseTimer snapshotTimer{Metrics::Phase::kSnapshot};
    PublishSnapshot(now);
  }
  ObserveRegression();
}

void Manager::SweepAwaitingArmorModels(const Settings &a_settings) {
  if (awaitingArmorModel_.empty()) {
    return;
  }
  const std::uint32_t now = NowMS();
  std::vector<RE::FormID> attached;
  std::vector<RE::FormID> expired;
  std::erase_if(awaitingArmorModel_,
                [&](std::pair<const RE::FormID, ArmorModelWait> &a_wait) {
                  const RE::FormID id = a_wait.first;
                  RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(id);
                  if (!actor || actor->IsDeleted() || !actor->Is3DLoaded()) {
                    return true;
                  }
                  if (BeyondEvictionRadius(*actor, a_settings)) {
                    return false;
                  }
                  if (!ArmorAwaitsModel(*actor)) {
                    attached.push_back(id);
                    return true;
                  }
                  if (!a_wait.second.expired &&
                      now - a_wait.second.sinceMS >= kArmorModelWaitMS) {
                    a_wait.second.expired = true;
                    expired.push_back(id);
                  }
                  return false;
                });
  for (const RE::FormID id : attached) {
    Trace::EmitSafely(
        Trace::Event::kApplication,
        {{"action", "armor_model_attached"}, {"actor", std::to_string(id)}});
    QueueRefresh(id);
  }
  for (const RE::FormID id : expired) {
    Trace::EmitSafely(Trace::Event::kApplication,
                      {{"action", "armor_model_wait_expired"},
                       {"actor", std::to_string(id)}});
    QueueRefresh(id);
  }
}

void Manager::SweepEviction(const Settings &a_settings) {
  if (a_settings.evictDistance <= 0.0f || a_settings.playerOnly) {
    evictedForDistance_.clear();
    return;
  }
  const auto *player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  const RE::NiPoint3 origin = player->GetPosition();
  const auto distanceOf = [&origin](RE::Actor &a_actor) {
    return origin.GetDistance(a_actor.GetPosition());
  };
  std::vector<RE::FormID> evict;
  for (const auto &[id, state] : applied_) {
    const RE::NiPointer<RE::Actor> actor = state.actor.get();
    if (actor && BeyondEvictionRadius(*actor, a_settings)) {
      evict.push_back(id);
    }
  }
  for (const RE::FormID id : evict) {
    Retire(id);
    evictedForDistance_.insert(id);
    Trace::EmitSafely(Trace::Event::kRetire,
                      {{"action", "evict_far"}, {"actor", std::to_string(id)}});
  }
  std::vector<RE::FormID> restore;
  for (const RE::FormID id : evictedForDistance_) {
    RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(id);
    if (!actor || EvictionFor(distanceOf(*actor), a_settings.evictDistance,
                              false) == EvictionAction::kRestore) {
      restore.push_back(id);
    }
  }
  for (const RE::FormID id : restore) {
    evictedForDistance_.erase(id);
    if (RE::TESForm::LookupByID<RE::Actor>(id)) {
      QueueRefresh(id);
    }
  }
}

void Manager::FireDueFinalizes() { applications_.FinalizeDue(NowMS()); }

void Manager::Tick(std::uint32_t a_nowMS, const Settings &a_settings) {
  tickNowMS_ = a_nowMS;
  const Studio::View &view = editor_.CurrentView();
  {
    const PhaseTimer previewsTimer{Metrics::Phase::kPreviews};
    TextureLab::GetSingleton()->RenderPreviews();
  }
  const TickFrame frame{a_settings, view, a_nowMS,
                        frozenLastTick_ && !view.freeze};
  frozenLastTick_ = view.freeze;
  std::size_t firstRenders = 0;
  for (auto it = applied_.begin(); it != applied_.end();) {
    LiveActor &state = it->second;
    const auto actor = state.actor.get();
    if (!actor || actor->IsDeleted() || !actor->Is3DLoaded()) {
      const RE::FormID id = it->first;
      AbandonApplications(id);
      ++it;
      Retire(id);
      continue;
    }
    DropLostGeometries(state);
    {
      const PhaseTimer signalsTimer{Metrics::Phase::kSignals};
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
    if (!AdmitRender(state, firstRenders)) {
      FinishApplications(it->first, state);
      ++it;
      continue;
    }
    std::vector<bool> hidden;
    {
      const PhaseTimer renderTimer{Metrics::Phase::kRender};
      hidden = RenderPieces(state, it->first);
    }
    {
      const PhaseTimer lightsTimer{Metrics::Phase::kLights};
      UpdateLights(state, hidden);
    }
    FinishApplications(it->first, state);
    if (Alive(state)) {
      ++it;
    } else {
      const RE::FormID id = it->first;
      ++it;
      Retire(id);
    }
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

void Manager::DropLostGeometries(LiveActor &a_state) {
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &bound : piece.geometries) {
      if (bound.lost) {
        continue;
      }
      if ((bound.material && !bound.material->StillOwned()) ||
          (bound.shell && !bound.shell->StillOwned())) {
        logger::info(
            "dropping '{}': its material or shell was replaced by another "
            "system",
            bound.name);
        RetireGeometry(a_state, bound);
      }
    }
  }
}

void Manager::RenderGeometry(LiveActor &a_state, LiveGeometry &a_bound,
                             bool a_hidden, bool a_inspected) {
  const TextureLab::PassBatch batch{*TextureLab::GetSingleton()};
  const Studio::View &view = editor_.CurrentView();
  if (a_bound.inputs.render &&
      a_bound.inputs.render->BeginFrame(renderFrame_, tickNowMS_)) {
    UpdateRenderInputs(a_state, a_bound);
  }
  if (a_bound.lost) {
    FailGeometryOutputs(a_state, a_bound);
    return;
  }
  const SlotRenderView slotView{
      view, view.isolation.layer.has_value() || !view.muted.empty(),
      GetSettings().publishEffects, !a_inspected};
  WriteSlots(a_state, a_bound, slotView, a_hidden);
  if (a_hidden || !slotView.publish) {
    if (a_bound.shell) {
      a_bound.shell->SetVisible(false);
    }
  } else {
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

bool Manager::Alive(const LiveActor &a_state) noexcept {
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      if (!bound.lost) {
        return true;
      }
    }
  }
  return std::ranges::any_of(a_state.instances, [](const LiveInstance &a_i) {
    return static_cast<bool>(a_i.light);
  });
}
}
