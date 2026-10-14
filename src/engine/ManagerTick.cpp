// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/ActorBudget.h"
#include "engine/ActorResize.h"
#include "engine/Clock.h"
#include "engine/LogOnce.h"
#include "engine/RegressionRun.h"
#include "engine/SectionTimer.h"
#include "orchestration/Tick.h"
#include "planners/RenderBudget.h"
#include "planners/RenderMemory.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "studio/ResolveOutput.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
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
  bool pending = false;
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
    if (auto updated = a_bound.inputs.render->Update(
            *instance.graph, i + 1, *instance.signals,
            MatchedShaderFactsOf(instance));
        !updated)
      LogOnce(a_warnings, Severity::kError,
              std::format("render inputs of '{}': {}", a_bound.name,
                          updated.error()));
  }
}

OutputRender OutputRenderOf(StackRender a_render) noexcept {
  switch (a_render) {
  case StackRender::kRendered:
    return OutputRender::kRendered;
  case StackRender::kPending:
    return OutputRender::kPending;
  case StackRender::kFailed:
    return OutputRender::kFailed;
  }
  return OutputRender::kFailed;
}

void RecordRender(PlacedOutput &a_output, StackRender a_render) {
  if (a_output.stack) {
    a_output.state = AfterRender(a_output.state, OutputRenderOf(a_render));
  }
}

void TraceStackFailure(const RenderOutput &a_stack, std::size_t a_output) {
  const std::span<const Diagnostic> problems = a_stack.Diagnostics();
  Trace::EmitSafely(
      Trace::Event::kApplication,
      {{"action", "stack_failed"},
       {"output", std::to_string(a_output)},
       {"problem", problems.empty() ? std::string{"the stack has no render"}
                                    : problems.front().message}});
}

StackRender RenderStackOutput(PlacedOutput &a_output,
                              const LayerFilter &a_filter,
                              const StackBase &a_base) {
  if (!a_output.stack) {
    return StackRender::kFailed;
  }
  const StackRender outcome =
      Compositor::GetSingleton()->Render(*a_output.stack, a_filter, a_base);
  RecordRender(a_output, outcome);
  if (outcome == StackRender::kFailed) {
    TraceStackFailure(*a_output.stack, a_output.index);
  }
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
    RecordRender(*output, outcome);
  }
  if (outcome != StackRender::kRendered) {
    SlotChain unshown;
    unshown.pending = outcome == StackRender::kPending;
    return unshown;
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
    if (const StackRender outcome = RenderStackOutput(*output, filter, base);
        outcome != StackRender::kRendered) {
      chain.pending = chain.pending || outcome == StackRender::kPending;
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
  const SignalState &signals = *instance.signals;
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

void AddBoundMeshes(BoundMeshes &a_meshes, const LivePiece &a_piece,
                    const Compositor &a_compositor) {
  for (const LiveGeometry &g : a_piece.geometries) {
    if (!g.lost) {
      a_meshes.geometries.push_back(g.geometry.get());
      a_meshes.materials.push_back(Compositor::MaterialKeyOf(
          g.inputs.material, a_compositor.MeshHashOf(g.geometry.get())));
    }
  }
}

BoundMeshes
BoundMeshesOf(const std::unordered_map<RE::FormID, LiveActor> &a_applied,
              const Compositor &a_compositor) {
  BoundMeshes meshes;
  for (const auto &[actorID, state] : a_applied) {
    for (const LivePiece &piece : state.pieces) {
      AddBoundMeshes(meshes, piece, a_compositor);
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
  const BoundMeshes meshes = BoundMeshesOf(a_applied, a_compositor);
  a_compositor.SweepMeshes(a_nowMS, meshes.geometries);
  a_compositor.SweepMaterials(a_nowMS, meshes.materials);
}

void FailGeometryOutputs(LiveActor &a_state, const LiveGeometry &a_bound) {
  for (const PlacementId id : a_bound.placements) {
    if (const std::optional<ResolvedPlacement> resolved =
            ResolvePlacement(a_state, id)) {
      for (PlacedOutput &output :
           a_state.placements[resolved->placement].outputs) {
        if (output.stack) {
          output.state = AfterLostGeometry(output.state);
        }
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

LiveActor *FirstDrawnOf(LiveActor &a_state) noexcept {
  if (a_state.restage && a_state.restage->firstDraw) {
    return a_state.restage.get();
  }
  return a_state.firstDraw ? &a_state : nullptr;
}

DrawPace DrawFirstSlots(LiveActor &a_state, const SlotRenderView &a_view,
                        DrawPace a_pace, std::uint64_t a_budgetUs) {
  TextureLab *lab = TextureLab::GetSingleton();
  if (!a_state.firstDraw || !lab || !MayDrawStack(a_pace, a_budgetUs)) {
    return a_pace;
  }
  const TextureLab::PassBatch batch{*lab};
  for (HiddenSlot &slot : a_state.firstDraw->slots) {
    if (slot.drawn) {
      continue;
    }
    if (!MayDrawStack(a_pace, a_budgetUs)) {
      break;
    }
    LiveGeometry *bound = GeometryOf(a_state, slot);
    if (!bound || bound->lost || slot.slot >= bound->stackPlan.slots.size()) {
      slot.drawn = true;
      continue;
    }
    const Metrics::Stopwatch watch;
    const SlotChain chain = RenderSlotChain(a_state, *bound, a_view,
                                            bound->stackPlan.slots[slot.slot]);
    slot.drawn = !chain.pending;
    a_pace = Drew(a_pace, watch.Micros());
  }
  return a_pace;
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

}

void Manager::OnFrame() {
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab) {
    return;
  }
  if (!textureLabStarted_) {
    textureLabStarted_ = true;
    const Metrics::Stopwatch startWatch;
    const bool started = lab->Init();
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
    heartbeatDue_ = true;
  }
  if (const std::optional<HeartbeatReading> heartbeat = RunFrame(*lab, now)) {
    EmitHeartbeat(*heartbeat);
  }
}

std::optional<HeartbeatReading> Manager::RunFrame(TextureLab &a_lab,
                                                  std::uint32_t a_nowMS) {
  const SectionTimer frameTimer{Metrics::Section::kFrame};
  a_lab.CollectTimings();
  SweepRetiredMaterialTextures();
  a_lab.CollectPreviewDraws();
  FireDueFinalizes();
  editor_.TickGesture();
  Metrics::CountFrame();
  ++renderFrame_;
  if (a_nowMS - lastSweepMS_ >= 1000) {
    lastSweepMS_ = a_nowMS;
    carriedInstances_.Expire(a_nowMS);
    SweepAnimationEvents();
  }
  const Settings settings = GetSettings();
  if (a_nowMS - lastTickMS_ < settings.TickIntervalMS()) {
    return std::nullopt;
  }
  lastTickMS_ = a_nowMS;
  std::optional<HeartbeatReading> heartbeat = RunTick(a_lab, a_nowMS, settings);
  {
    const SectionTimer sweepTimer{Metrics::Section::kSweep};
    SweepBoundMeshes(*Compositor::GetSingleton(), live_, a_nowMS);
  }
  {
    const SectionTimer snapshotTimer{Metrics::Section::kSnapshot};
    PublishSnapshot(a_nowMS);
  }
  ObserveRegression();
  return heartbeat;
}

std::optional<HeartbeatReading> Manager::RunTick(TextureLab &a_lab,
                                                 std::uint32_t a_nowMS,
                                                 const Settings &a_settings) {
  tickNowMS_ = a_nowMS;
  std::vector<Orchestration::PlanJobDone> jobs = DrainPlanJobs();
  {
    const SectionTimer sweepTimer{Metrics::Section::kSweep};
    Compositor::GetSingleton()->BeginTick(a_nowMS);
    Compositor::GetSingleton()->SweepStepOutputs(a_nowMS);
    a_lab.ExpireIdleTargets(a_nowMS);
  }
  std::optional<HeartbeatReading> heartbeat;
  if (heartbeatDue_) {
    heartbeatDue_ = false;
    const Metrics::Snapshot measured = Metrics::Drain();
    const BudgetSample sample = SampleBudget(measured, a_settings);
    heartbeat = HeartbeatReading{measured, sample, a_nowMS - lastMetricsMS_};
    lastMetricsMS_ = a_nowMS;
    pendingSample_ = sample;
  }
  const SectionTimer tickTimer{Metrics::Section::kTick};
  const GatheredFacts gathered =
      GatherFacts(a_nowMS, a_settings, std::move(jobs));
  CheckAgreement(gathered.facts);
  Orchestration::TickOutput out =
      Orchestration::Tick(std::move(world_), gathered.facts);
  world_ = std::move(out.world);
  EmitCoreRecords(out.trace, out.logs);
  const Studio::View &view = editor_.CurrentView();
  const TickContext context{a_settings,
                            view,
                            InspectedActor(a_nowMS),
                            a_nowMS,
                            frozenLastTick_ && !view.freeze,
                            gathered};
  frozenLastTick_ = view.freeze;
  {
    const SectionTimer previewsTimer{Metrics::Section::kPreviews};
    a_lab.RenderPreviews();
  }
  a_lab.SetGeneratedShaders(a_settings.generatedShaders);
  a_lab.BeginTimedTick(a_settings.gpuTiming);
  results_ = Execute(out.commands, context);
  a_lab.EndTimedTick();
  return heartbeat;
}

void Manager::FireDueFinalizes() { applications_.FinalizeDue(NowMS()); }

std::vector<bool> Manager::RenderPieces(LiveActor &a_state,
                                        RE::FormID a_actorID,
                                        const RenderFlags &a_flags) {
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
      RenderGeometry(a_state, bound, pieceHidden, inspected, a_flags);
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

void Manager::RenderGeometry(LiveActor &a_state, LiveGeometry &a_bound,
                             bool a_pieceHidden, bool a_inspected,
                             const RenderFlags &a_flags) {
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab) {
    return;
  }
  const TextureLab::PassBatch batch{*lab};
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
    WriteSlots(a_state, a_bound, slotView,
               a_pieceHidden || a_flags.concealSlots);
  }
  if (a_pieceHidden || a_flags.hideShells) {
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

CommandResults Manager::RunTickActor(const Orchestration::TickActor &a_command,
                                     const TickContext &a_context) {
  const RE::FormID id = Orchestration::FormOf(a_command.actor);
  const auto found = live_.find(id);
  if (found == live_.end()) {
    return {};
  }
  LiveActor &state = found->second;
  if (!state.actor.get()) {
    return {};
  }
  {
    const SectionTimer signalsTimer{Metrics::Section::kSignals};
    const TickFrame frame{a_context.settings, a_context.view, a_context.nowMS,
                          a_context.resuming};
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
  std::vector<bool> hidden;
  if (a_command.drawStacks) {
    const SectionTimer renderTimer{Metrics::Section::kRender};
    state.holding = IntermediateHolding::kHeld;
    hidden =
        RenderPieces(state, id, {a_command.concealSlots, a_command.hideShells});
  }
  if (a_command.hideLights) {
    hidden.assign(state.instances.size(), true);
  }
  {
    const SectionTimer lightsTimer{Metrics::Section::kLights};
    UpdateLights(state, hidden);
  }
  if (a_command.finishApplications) {
    FinishApplications(id, state, a_command.drawStacks);
  }
  if (!a_command.concealSlots) {
    state.firstDraw.reset();
  }
  return {};
}

CommandResults
Manager::RunDrawUnpublished(const Orchestration::DrawUnpublished &a_command,
                            const TickContext &a_context) {
  const SectionTimer renderTimer{Metrics::Section::kRender};
  const Studio::View &view = a_context.view;
  CommandResults results;
  DrawPace pace;
  for (const Orchestration::ActorId actor : a_command.order) {
    const RE::FormID id = Orchestration::FormOf(actor);
    const auto found = live_.find(id);
    if (found == live_.end()) {
      ReportDisagreement(id, {"command", "draw_unpublished"},
                         "no resources to draw");
      continue;
    }
    LiveActor &state = found->second;
    LiveActor *firstDrawn = FirstDrawnOf(state);
    const SlotRenderView slotView{
        view, view.isolation.layer.has_value() || !view.muted.empty(),
        a_context.settings.publishEffects, a_context.inspected != id};
    const std::size_t before = pace.stacks;
    std::size_t drawn = 0;
    if (InFlightOf(state) && !state.restage) {
      pace = DrawResize(state, renderFrame_, a_context.nowMS, warningHistory_,
                        pace, a_command.budgetUs);
      const ResizeInFlight *resize = InFlightOf(state);
      drawn = resize ? DrawnStacks(*resize) : 0;
    } else if (firstDrawn) {
      pace = DrawFirstSlots(*firstDrawn, slotView, pace, a_command.budgetUs);
      drawn = firstDrawn->firstDraw ? DrawnSlots(*firstDrawn->firstDraw) : 0;
    } else {
      ReportDisagreement(id, {"command", "draw_unpublished"},
                         "nothing unpublished to draw");
      continue;
    }
    results.emplace_back(Orchestration::ActorEvent{
        actor, Orchestration::UnpublishedDrawn{drawn, pace.stacks > before}});
  }
  if (pace.stacks > 0) {
    Metrics::CountHiddenDraw(pace.stacks, pace.spentUs);
  }
  return results;
}
}
