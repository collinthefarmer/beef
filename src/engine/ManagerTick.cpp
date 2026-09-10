#include "engine/Manager.h"

#include "SettingsFile.h"
#include "render/Compositor.h"
#include "render/RuntimeTextures.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }

SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface) {
  if (a_surface == Surface::kShell) {
    return a_bound.shell.get();
  }
  return a_bound.material.get();
}

PlacedOutput *OutputAt(LivePlacement &a_placement, std::size_t a_index) {
  for (PlacedOutput &output : a_placement.outputs) {
    if (output.index == a_index) {
      return &output;
    }
  }
  return nullptr;
}

struct SlotWrite {
  Slot slot = Slot::kEmissive;
  RE::NiSourceTexture *texture = nullptr;
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

void TakeScalar(SlotWrite &a_write, ScalarField a_field,
                const SlotScalars &a_scalars, const SignalState &a_signals) {
  if (a_field == ScalarField::kColor) {
    if (a_scalars.color) {
      a_write.color = a_signals.Resolve(*a_scalars.color);
    }
  } else if (const std::optional<Param> *param = ScalarOf(a_scalars, a_field);
             param && *param) {
    a_write.scalars[static_cast<std::size_t>(a_field)] =
        a_signals.Resolve(**param);
  }
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
    a_target.WriteGlint(a_write.Of(ScalarField::kScreenSpaceScale),
                        a_write.Of(ScalarField::kLogMicrofacetDensity),
                        a_write.Of(ScalarField::kMicrofacetRoughness),
                        a_write.Of(ScalarField::kDensityRandomization),
                        a_write.shown);
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
}

void Manager::OnFrame() {
  FireDueFinalizes();
  const std::uint32_t now = NowMS();
  if (now - lastTickMS_ < GetSettings().TickIntervalMS()) {
    return;
  }
  lastTickMS_ = now;
  if (!applied_.empty()) {
    Tick(now);
  }
  PublishSnapshot(now);
}

void Manager::FireDueFinalizes() {
  std::vector<RE::FormID> due;
  {
    std::scoped_lock lock{queueLock_};
    const std::uint32_t now = NowMS();
    for (auto it = finalizeDue_.begin(); it != finalizeDue_.end();) {
      if (static_cast<std::int32_t>(now - it->second) >= 0) {
        due.push_back(it->first);
        equipped_.insert(it->first);
        it = finalizeDue_.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (const RE::FormID id : due) {
    QueueRefresh(id);
  }
}

void Manager::Tick(std::uint32_t a_nowMS) {
  const Settings &settings = GetSettings();
  Compositor *compositor = Compositor::GetSingleton();
  compositor->BeginTick(a_nowMS);
  TextureLab::GetSingleton()->RenderPreviews();
  const bool resuming = frozenLastTick_ && !view_.freeze;
  frozenLastTick_ = view_.freeze;
  for (auto it = applied_.begin(); it != applied_.end();) {
    LiveActor &state = it->second;
    DropLostGeometries(state);
    for (LiveInstance &instance : state.instances) {
      if (!instance.recipe || !instance.graph || !instance.signals ||
          !instance.environment) {
        continue;
      }
      const float speed =
          settings.animationSpeed * view_.speed * instance.recipe->clock.speed;
      if (resuming && speed > 0.0f) {
        instance.startMS = a_nowMS - static_cast<std::uint32_t>(
                                         view_.scrubSeconds / speed * 1000.0f);
      }
      const float time =
          view_.freeze
              ? view_.scrubSeconds
              : static_cast<float>(a_nowMS - instance.startMS) * 0.001f * speed;
      const float delta = std::max(0.0f, time - instance.lastTime);
      TickInstance(instance, time, delta);
      instance.lastTime = time;
    }
    for (LivePiece &piece : state.pieces) {
      for (LiveGeometry &bound : piece.geometries) {
        RenderGeometry(state, piece, bound);
      }
    }
    UpdateLights(state);
    it = Alive(state) ? std::next(it) : applied_.erase(it);
  }
  if (compositor->MeshSweepDue(a_nowMS)) {
    std::vector<RE::BSGeometry *> bound;
    for (const auto &[actorID, state] : applied_) {
      for (const LivePiece &piece : state.pieces) {
        for (const LiveGeometry &g : piece.geometries) {
          if (!g.lost) {
            bound.push_back(g.geometry.get());
          }
        }
      }
    }
    compositor->SweepMeshes(a_nowMS, bound);
  }
}

void Manager::TickInstance(LiveInstance &a_instance, float a_time,
                           float a_delta) {
  if (!a_instance.graph || !a_instance.signals || !a_instance.environment) {
    return;
  }
  if (view_.freeze && a_time + 0.001f < a_instance.lastTime) {
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
        bound.lost = true;
        bound.material.reset();
        bound.shell.reset();
        bound.shellOwner.reset();
        bound.plan = GeometryPlan{};
        bound.stackPlan = GeometryStackPlan{};
        bound.binding = BindingDiff{};
      }
    }
  }
}

void Manager::RenderGeometry(LiveActor &a_state,
                             [[maybe_unused]] LivePiece &a_piece,
                             LiveGeometry &a_bound) {
  if (a_bound.lost) {
    return;
  }
  const bool anyLayerHidden = view_.isolateLayer >= 0 || !view_.muted.empty();
  for (const SlotStackPlan &slot : a_bound.stackPlan.slots) {
    SlotTarget *target = TargetFor(a_bound, slot.surface);
    if (!target) {
      continue;
    }
    SlotWrite write = EmptyWrite(slot.slot);
    StackBase base;
    std::vector<const SurfaceOutput *> shown;
    std::vector<const SignalState *> signals;
    for (const StackLink &link : slot.chain) {
      const SlotContribution c = link.contribution;
      const std::size_t placed = static_cast<std::size_t>(c.placed);
      if (placed >= a_bound.placements.size()) {
        continue;
      }
      const std::size_t placementIndex =
          static_cast<std::size_t>(a_bound.placements[placed]);
      if (placementIndex >= a_state.structure.placements.size() ||
          placementIndex >= a_state.placements.size()) {
        continue;
      }
      const Placement &placement = a_state.structure.placements[placementIndex];
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
          !view_.OutputShown(instance.recipe->id, c.output)) {
        continue;
      }
      LayerFilter filter;
      if (anyLayerHidden) {
        filter = HiddenLayers(view_, instance.recipe->id, c.output,
                              material->stack.size());
      }
      Compositor::GetSingleton()->Render(*output->stack, *instance.signals,
                                         instance.lastTime, filter, base);
      write.shown = true;
      if (RE::NiSourceTexture *texture = output->stack->Texture()) {
        base = StackBase{texture, base.animated || output->stack->Animated()};
        write.texture = texture;
      }
      shown.push_back(material);
      signals.push_back(instance.signals.get());
    }
    for (const ScalarField field : ScalarsOf(slot.slot)) {
      if (const std::optional<std::size_t> at =
              ScalarSource(slot.slot, field, shown)) {
        TakeScalar(write, field, shown[*at]->scalars, *signals[*at]);
      }
    }
    WriteSlot(*target, write);
  }
  if (a_bound.shell && a_bound.shellOwner &&
      *a_bound.shellOwner < a_state.instances.size()) {
    LiveInstance &instance = a_state.instances[*a_bound.shellOwner];
    if (instance.recipe && instance.signals) {
      const ShellSettings &shell = instance.recipe->shell;
      SignalState &signals = *instance.signals;
      a_bound.shell->Pose(
          signals.Resolve(shell.pose.inflate), signals.Resolve(shell.alpha),
          signals.Resolve(shell.rimPower), signals.Resolve(shell.emissive));
      a_bound.shell->SetVisible(view_.RecipeShown(instance.recipe->id));
    }
  }
}

void Manager::UpdateLights(LiveActor &a_state) {
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
    SignalState &signals = *instance.signals;
    instance.light->Update(
        signals.Resolve(light->color), signals.Resolve(light->intensity),
        signals.Resolve(light->size), signals.Resolve(light->cutoff),
        view_.OutputShown(instance.recipe->id, *instance.lightOutput));
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
