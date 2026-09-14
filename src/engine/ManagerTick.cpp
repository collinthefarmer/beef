#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/Clock.h"
#include "render/Compositor.h"
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
    return scalars[static_cast<std::size_t>(a_field)];
  }
  [[nodiscard]] float Shown(ScalarField a_field) const {
    return shown ? Of(a_field) : 0.0f;
  }
};

SlotWrite EmptyWrite(Slot a_slot) {
  SlotWrite write;
  write.slot = a_slot;
  for (const ScalarField field : ScalarsOf(a_slot)) {
    write.scalars[static_cast<std::size_t>(field)] = ScalarFallback(field);
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

LayerFilter HiddenLayers(const Studio::View &a_view,
                         const std::string &a_recipe, std::size_t a_output,
                         std::size_t a_layerCount) {
  LayerFilter filter;
  if (!a_view.FiltersLayers(a_recipe, a_output)) {
    return filter;
  }
  for (std::size_t i = 0; i < a_layerCount; ++i) {
    if (!a_view.LayerShown(a_recipe, a_output, i)) {
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

SlotChain RenderSlotChain(LiveActor &a_state, LiveGeometry &a_bound,
                          const Studio::View &a_view,
                          const SlotStackPlan &a_slot, bool a_anyLayerHidden) {
  SlotChain chain;
  StackBase base;
  for (const StackLink &link : a_slot.chain) {
    const SlotContribution c = link.contribution;
    const std::size_t placed = static_cast<std::size_t>(c.placed);
    if (placed >= a_bound.placements.size()) {
      continue;
    }
    const std::size_t placementIndex =
        static_cast<std::size_t>(a_bound.placements[placed]);
    if (placementIndex >= a_state.plan.placements.size() ||
        placementIndex >= a_state.placements.size()) {
      continue;
    }
    const Placement &placement = a_state.plan.placements[placementIndex];
    const std::size_t instanceIndex =
        static_cast<std::size_t>(placement.instance);
    if (instanceIndex >= a_state.instances.size()) {
      continue;
    }
    LiveInstance &instance = a_state.instances[instanceIndex];
    if (!instance.recipe || !instance.signals) {
      continue;
    }
    const SurfaceOutput *material =
        c.output < instance.recipe->outputs.size()
            ? Get<SurfaceOutput>(instance.recipe->outputs[c.output])
            : nullptr;
    PlacedOutput *output =
        OutputAt(a_state.placements[placementIndex], c.output);
    if (!material || !output || !output->stack ||
        !a_view.OutputShown(instance.recipe->id, c.output)) {
      continue;
    }
    LayerFilter filter;
    if (a_anyLayerHidden) {
      filter = HiddenLayers(a_view, instance.recipe->id, c.output,
                            material->stack.size());
    }
    const bool rendered = Compositor::GetSingleton()->Render(
        *output->stack, *instance.signals, instance.lastTime, filter, base);
    output->rendered = rendered;
    output->renderFailed = !rendered;
    if (!rendered) {
      continue;
    }
    chain.shown = true;
    if (TextureRef texture = output->stack->Texture()) {
      base = StackBase{texture, base.animated || output->stack->Animated()};
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
      a_write.scalars[static_cast<std::size_t>(field)] =
          a_chain.resolved[*at].Scalar(field);
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
  a_bound.shell->Pose(
      signals.Resolve(shell.pose.inflate), signals.Resolve(shell.alpha),
      signals.Resolve(shell.rimPower), signals.Resolve(shell.emissive));
  a_bound.shell->SetVisible(a_view.RecipeShown(instance.recipe->id));
}

struct InstanceTiming {
  float time = 0.0f;
  float delta = 0.0f;
};

InstanceTiming InstanceTimeFor(LiveInstance &a_instance,
                               const Settings &a_settings,
                               const Studio::View &a_view, bool a_resuming,
                               std::uint32_t a_nowMS) {
  const float speed =
      a_settings.animationSpeed * a_view.speed * a_instance.recipe->clock.speed;
  if (a_resuming && speed > 0.0f) {
    a_instance.startMS = a_nowMS - static_cast<std::uint32_t>(
                                       a_view.scrubSeconds / speed * 1000.0f);
  }
  const float time =
      a_view.freeze
          ? a_view.scrubSeconds
          : static_cast<float>(a_nowMS - a_instance.startMS) * 0.001f * speed;
  const float delta = std::max(0.0f, time - a_instance.lastTime);
  return InstanceTiming{time, delta};
}

void SweepBoundMeshes(
    Compositor &a_compositor,
    const std::unordered_map<RE::FormID, LiveActor> &a_applied,
    std::uint32_t a_nowMS) {
  if (!a_compositor.MeshSweepDue(a_nowMS)) {
    return;
  }
  std::vector<RE::BSGeometry *> bound;
  for (const auto &[actorID, state] : a_applied) {
    for (const LivePiece &piece : state.pieces) {
      for (const LiveGeometry &g : piece.geometries) {
        if (!g.lost) {
          bound.push_back(g.geometry.get());
        }
      }
    }
  }
  a_compositor.SweepMeshes(a_nowMS, bound);
}
}

void Manager::OnFrame() {
  if (applications_.Loading()) {
    return;
  }
  SweepRetiredMaterialTextures();
  TextureLab::GetSingleton()->CollectPreviewDraws();
  FireDueFinalizes();
  editor_.TickGesture();
  const std::uint32_t now = NowMS();
  const Settings settings = GetSettings();
  if (now - lastTickMS_ < settings.TickIntervalMS()) {
    return;
  }
  lastTickMS_ = now;
  if (!applied_.empty()) {
    Tick(now, settings);
  }
  PublishSnapshot(now);
}

void Manager::FireDueFinalizes() { applications_.FinalizeDue(NowMS()); }

void Manager::Tick(std::uint32_t a_nowMS, const Settings &a_settings) {
  const Studio::View &view = editor_.CurrentView();
  Compositor *compositor = Compositor::GetSingleton();
  compositor->BeginTick(a_nowMS);
  TextureLab::GetSingleton()->RenderPreviews();
  const bool resuming = frozenLastTick_ && !view.freeze;
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
      const InstanceTiming timing =
          InstanceTimeFor(instance, a_settings, view, resuming, a_nowMS);
      TickInstance(instance, timing.time, timing.delta);
      instance.lastTime = timing.time;
    }
    for (LivePiece &piece : state.pieces) {
      for (LiveGeometry &bound : piece.geometries) {
        RenderGeometry(state, piece, bound);
      }
    }
    UpdateLights(state);
    FinishApplications(it->first, state);
    if (Alive(state)) {
      ++it;
    } else {
      const RE::FormID id = it->first;
      ++it;
      Retire(id);
    }
  }
  SweepBoundMeshes(*compositor, applied_, a_nowMS);
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
        RetireGeometry(bound);
      }
    }
  }
}

void Manager::RenderGeometry(LiveActor &a_state,
                             [[maybe_unused]] LivePiece &a_piece,
                             LiveGeometry &a_bound) {
  const Studio::View &view = editor_.CurrentView();
  if (a_bound.lost) {
    for (const PlacementId id : a_bound.placements) {
      const auto index = static_cast<std::size_t>(id);
      if (index >= a_state.placements.size()) {
        continue;
      }
      for (PlacedOutput &output : a_state.placements[index].outputs) {
        output.renderFailed = true;
      }
    }
    return;
  }
  const bool anyLayerHidden =
      view.isolation.layer.has_value() || !view.muted.empty();
  for (const SlotStackPlan &slot : a_bound.stackPlan.slots) {
    SlotTarget *target = TargetFor(a_bound, slot.surface);
    if (!target) {
      continue;
    }
    SlotWrite write = EmptyWrite(slot.slot);
    const SlotChain chain =
        RenderSlotChain(a_state, a_bound, view, slot, anyLayerHidden);
    write.shown = chain.shown;
    write.texture = chain.texture;
    ApplySlotScalars(write, slot.slot, chain);
    WriteSlot(*target, write);
  }
  PoseShell(a_bound, a_state, view);
}

void Manager::UpdateLights(LiveActor &a_state) {
  const Studio::View &view = editor_.CurrentView();
  for (LiveInstance &instance : a_state.instances) {
    if (!instance.light || !instance.lightOutput || !instance.recipe ||
        !instance.signals) {
      continue;
    }
    const LightOutput *light =
        *instance.lightOutput < instance.recipe->outputs.size()
            ? Get<LightOutput>(instance.recipe->outputs[*instance.lightOutput])
            : nullptr;
    if (!light) {
      continue;
    }
    const Studio::ResolvedLight resolved =
        Studio::ResolveLight(*light, *instance.signals);
    instance.light->Update(
        resolved.color, resolved.intensity, resolved.size, resolved.cutoff,
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
