// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/TargetCensus.h"

#include "diagnostics/Trace.h"
#include "engine/ActorResize.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetHolders.h"
#include "render/Binding.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"
#include "render/TextureRef.h"

#include <format>
#include <map>
#include <span>
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
  if (const ResizeInFlight *inFlight = InFlightOf(a_state)) {
    if (inFlight->render) {
      a_instances.insert(inFlight->render.get());
    }
    for (const ResizedStack &resized : inFlight->stacks) {
      CollectOutput(resized.stack, a_sets);
    }
  }
}

HolderSet SlotTargetsOf(const LiveActor &a_state) {
  HolderSet slots;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.material) {
        geometry.material->CollectTargets(slots);
      }
      if (geometry.shell) {
        geometry.shell->CollectTargets(slots);
      }
    }
  }
  return slots;
}

bool InViewNow(const Orchestration::World &a_world, RE::FormID a_actor) {
  const auto found = a_world.actors.find(Orchestration::ActorOf(a_actor));
  return found != a_world.actors.end() && found->second.sight.inView;
}

std::string
SlotOnlyText(const std::unordered_map<RE::FormID, LiveActor> &a_live,
             const Orchestration::World &a_world, const HolderSets &a_sets,
             std::span<const LiveTarget> a_targets) {
  std::unordered_map<std::uintptr_t, const LiveTarget *> byId;
  for (const LiveTarget &target : a_targets) {
    byId.emplace(target.id, &target);
  }
  const HolderSet &instance = SetOf(a_sets, TargetHolder::kInstance);
  const HolderSet &output = SetOf(a_sets, TargetHolder::kOutput);
  std::string text;
  for (const auto &[id, state] : a_live) {
    std::map<std::uint32_t, std::size_t> sides;
    std::uint64_t bytes = 0;
    for (const std::uintptr_t target : SlotTargetsOf(state)) {
      const auto found = byId.find(target);
      if (found == byId.end() || instance.contains(target) ||
          output.contains(target)) {
        continue;
      }
      ++sides[found->second->side];
      bytes += found->second->bytes;
    }
    if (sides.empty()) {
      continue;
    }
    std::string sideText;
    for (const auto &[side, count] : sides) {
      sideText +=
          std::format("{}{}x{}", sideText.empty() ? "" : ",", count, side);
    }
    text += std::format(
        "{}actor {} level {} {} bytes {} sides {}", text.empty() ? "" : "; ",
        id, LevelName(state.renderLevel),
        InViewNow(a_world, id) ? "in view" : "out of view", bytes, sideText);
  }
  return text;
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

void EmitTargetHolders(const std::unordered_map<RE::FormID, LiveActor> &a_live,
                       const Orchestration::World &a_world,
                       std::uint64_t a_disagreements) {
  Trace::Safely([&] {
    Instances instances;
    const HolderSets sets = HolderSetsOf(a_live, instances);
    const std::vector<LiveTarget> live = LiveTextureTargets();
    const HolderTallies tallies = TallyHolders(live, sets);
    std::map<std::string, HeldTally> unplanned;
    for (const RenderInstance *instance : instances) {
      instance->CollectUnplannedOutputs(unplanned);
    }
    std::string unplannedText;
    for (const auto &[shape, tally] : unplanned) {
      unplannedText += std::format("{}{}: {} targets {} bytes",
                                   unplannedText.empty() ? "" : "; ", shape,
                                   tally.targets, tally.bytes);
    }
    std::vector<Trace::Field> fields{
        {"action", "target_holders"},
        {"live_targets", std::to_string(live.size())},
        {"live_instances", std::to_string(RenderInstance::LiveCount())},
        {"attached_instances", std::to_string(instances.size())},
        {"unplanned", unplannedText},
        {"slot_only", SlotOnlyText(a_live, a_world, sets, live)},
        {"resource_disagreements", std::to_string(a_disagreements)}};
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
