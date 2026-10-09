// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/TargetCensus.h"

#include "diagnostics/Trace.h"
#include "engine/ActorResize.h"
#include "planners/TargetHolders.h"
#include "render/Binding.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "render/TextureRef.h"

#include <format>
#include <string>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
using Instances = std::unordered_set<const RenderInstance *>;

void CollectOutput(const std::unique_ptr<RenderOutput> &a_output,
                   HolderSets &a_sets) {
  if (a_output) {
    a_output->Texture().CollectTarget(SetOf(a_sets, TargetHolder::kOutput));
  }
}

void CollectGeometry(const LiveGeometry &a_geometry, HolderSets &a_sets,
                     Instances &a_instances) {
  if (a_geometry.inputs.render) {
    a_instances.insert(a_geometry.inputs.render.get());
  }
  a_geometry.inputs.material.CollectTargets(
      SetOf(a_sets, TargetHolder::kMaterialInput));
  if (a_geometry.material) {
    a_geometry.material->CollectTargets(SetOf(a_sets, TargetHolder::kSlot));
  }
  if (a_geometry.shell) {
    a_geometry.shell->CollectTargets(SetOf(a_sets, TargetHolder::kSlot));
  }
  for (const MergedSlotStack &merged : a_geometry.mergedSlots) {
    CollectOutput(merged.stack, a_sets);
  }
}

void CollectActor(const LiveActor &a_state, HolderSets &a_sets,
                  Instances &a_instances) {
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      CollectGeometry(geometry, a_sets, a_instances);
    }
  }
  for (const LivePlacement &placement : a_state.placements) {
    for (const PlacedOutput &output : placement.outputs) {
      CollectOutput(output.stack, a_sets);
    }
  }
  if (const ResizeInFlight *inFlight = Get<ResizeInFlight>(a_state.resize)) {
    if (inFlight->render) {
      a_instances.insert(inFlight->render.get());
    }
    for (const ResizedStack &resized : inFlight->stacks) {
      CollectOutput(resized.stack, a_sets);
    }
  }
}

HolderSets HolderSetsOf(const std::unordered_map<RE::FormID, LiveActor> &a_live,
                        Instances &a_instances) {
  HolderSets sets;
  for (const auto &[id, state] : a_live) {
    CollectActor(state, sets, a_instances);
  }
  for (const RenderInstance *instance : a_instances) {
    instance->CollectOutputTargets(SetOf(sets, TargetHolder::kInstance));
    instance->CollectInputTargets(SetOf(sets, TargetHolder::kMaterialInput));
  }
  CollectRetiredTextureTargets(SetOf(sets, TargetHolder::kRetired));
  if (const Compositor *compositor = Compositor::GetSingleton()) {
    compositor->CollectCachedTargets(SetOf(sets, TargetHolder::kCache));
    compositor->CollectResidentTargets(SetOf(sets, TargetHolder::kResident));
  }
  if (TextureLab *lab = TextureLab::GetSingleton()) {
    lab->CollectPreviewTargets(SetOf(sets, TargetHolder::kPreview));
    lab->CollectIdleTargets(SetOf(sets, TargetHolder::kPool));
  }
  return sets;
}
}

void EmitTargetHolders(
    const std::unordered_map<RE::FormID, LiveActor> &a_live) {
  Trace::Safely([&] {
    Instances instances;
    const HolderSets sets = HolderSetsOf(a_live, instances);
    const std::vector<LiveTarget> live = LiveTextureTargets();
    const HolderTallies tallies = TallyHolders(live, sets);
    std::vector<Trace::Field> fields{
        {"action", "target_holders"},
        {"live_targets", std::to_string(live.size())},
        {"live_instances", std::to_string(RenderInstance::LiveCount())},
        {"attached_instances", std::to_string(instances.size())}};
    for (const TargetHolder holder : kTargetHolders) {
      const HeldTally &tally = TallyOf(tallies, holder);
      fields.push_back({std::format("{}_targets", HolderName(holder)),
                        std::to_string(tally.targets)});
      fields.push_back({std::format("{}_bytes", HolderName(holder)),
                        std::to_string(tally.bytes)});
    }
    Trace::Emit(Trace::Event::kMetrics, fields);
  });
}
}
