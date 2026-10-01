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
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {

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

SlotChain RenderSlotChain(LiveActor &a_state, LiveGeometry &a_bound,
                          const SlotRenderView &a_view,
                          const SlotStackPlan &a_slot) {
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

void EmitMetricsHeartbeat() {
  EmitGpuTimings();
  const Metrics::Snapshot measured = Metrics::Drain();
  Trace::EmitSafely(
      Trace::Event::kMetrics,
      {{"action", "heartbeat"},
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
       {"frames", std::to_string(measured.frames)},
       {"render_evaluations", std::to_string(measured.renderEvaluations)},
       {"step_executions", std::to_string(measured.stepExecutions)},
       {"step_releases", std::to_string(measured.stepReleases)},
       {"step_restores", std::to_string(measured.stepRestores)},
       {"frame_us", std::to_string(measured.frame.micros)},
       {"frame_max_us", std::to_string(measured.frame.maxMicros)},
       {"tick_us", std::to_string(measured.tick.micros)},
       {"tick_max_us", std::to_string(measured.tick.maxMicros)},
       {"snapshot_us", std::to_string(measured.snapshot.micros)},
       {"snapshot_max_us", std::to_string(measured.snapshot.maxMicros)}});
}
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
  if (now - lastModelCheckMS_ >= 100) {
    lastModelCheckMS_ = now;
    SweepAwaitingModels(settings);
  }
  if (now - lastEvictionMS_ >= 1000) {
    lastEvictionMS_ = now;
    SweepEviction(settings);
  }
  if (now - lastTickMS_ < settings.TickIntervalMS()) {
    return;
  }
  lastTickMS_ = now;
  Compositor::GetSingleton()->BeginTick(now);
  if (!applied_.empty()) {
    const PhaseTimer tickTimer{Metrics::Phase::kTick};
    TextureLab::GetSingleton()->SetGeneratedShaders(settings.generatedShaders);
    TextureLab::GetSingleton()->BeginTimedTick(settings.gpuTiming);
    Tick(now, settings);
    TextureLab::GetSingleton()->EndTimedTick();
  }
  SweepBoundMeshes(*Compositor::GetSingleton(), applied_, now);
  {
    const PhaseTimer snapshotTimer{Metrics::Phase::kSnapshot};
    PublishSnapshot(now);
  }
  ObserveRegression();
}

void Manager::SweepAwaitingModels(const Settings &a_settings) {
  if (awaitingModel_.empty()) {
    return;
  }
  const auto *player = RE::PlayerCharacter::GetSingleton();
  const auto beyondRadius = [&](const RE::Actor &a_actor) {
    return player && !a_actor.IsPlayerRef() &&
           a_settings.evictDistance > 0.0f &&
           EvictionFor(player->GetPosition().GetDistance(a_actor.GetPosition()),
                       a_settings.evictDistance,
                       true) == EvictionAction::kEvict;
  };
  std::vector<RE::FormID> attached;
  std::erase_if(awaitingModel_, [&](RE::FormID a_id) {
    RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
    if (!actor || actor->IsDeleted() || !actor->Is3DLoaded()) {
      return true;
    }
    if (beyondRadius(*actor) || ArmorAwaitsModel(*actor)) {
      return false;
    }
    attached.push_back(a_id);
    return true;
  });
  for (const RE::FormID id : attached) {
    Trace::EmitSafely(
        Trace::Event::kApplication,
        {{"action", "armor_model_attached"}, {"actor", std::to_string(id)}});
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
    if (!actor || actor->IsPlayerRef()) {
      continue;
    }
    if (EvictionFor(distanceOf(*actor), a_settings.evictDistance, true) ==
        EvictionAction::kEvict) {
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
  const Studio::View &view = editor_.CurrentView();
  TextureLab::GetSingleton()->RenderPreviews();
  const TickFrame frame{a_settings, view, a_nowMS,
                        frozenLastTick_ && !view.freeze};
  frozenLastTick_ = view.freeze;
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
    for (LiveInstance &instance : state.instances) {
      if (!instance.recipe || !instance.graph || !instance.signals ||
          !instance.environment) {
        continue;
      }
      const InstanceTiming timing = InstanceTimeFor(instance, frame);
      TickInstance(instance, timing.time, timing.delta);
      instance.lastTime = timing.time;
    }
    UpdateLights(state, RenderPieces(state, it->first));
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
      RenderGeometry(a_state, bound, pieceHidden);
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
                             bool a_hidden) {
  const Studio::View &view = editor_.CurrentView();
  if (a_bound.inputs.render &&
      a_bound.inputs.render->BeginFrame(renderFrame_)) {
    UpdateRenderInputs(a_state, a_bound);
  }
  if (a_bound.lost) {
    FailGeometryOutputs(a_state, a_bound);
    return;
  }
  const SlotRenderView slotView{
      view, view.isolation.layer.has_value() || !view.muted.empty(),
      GetSettings().publishEffects};
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
