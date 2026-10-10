// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorResize.h"

#include "diagnostics/Metrics.h"
#include "engine/LogOnce.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"

#include <algorithm>
#include <format>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
using StackHolder = std::unique_ptr<RenderOutput> *;

std::vector<StackHolder> StackHolders(LiveActor &a_state) {
  std::vector<StackHolder> holders;
  for (LivePlacement &placement : a_state.placements) {
    for (PlacedOutput &output : placement.outputs) {
      if (output.stack) {
        holders.push_back(&output.stack);
      }
    }
  }
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      for (MergedSlotStack &merged : geometry.mergedSlots) {
        if (merged.stack) {
          holders.push_back(&merged.stack);
        }
      }
    }
  }
  return holders;
}

using MergedLink = std::pair<std::size_t, std::size_t>;

void AddMergedLinks(const LiveActor &a_state, const LiveGeometry &a_geometry,
                    std::set<MergedLink> &a_links, std::size_t &a_stacks) {
  for (const MergedSlotStack &merged : a_geometry.mergedSlots) {
    if (!merged.stack) {
      continue;
    }
    ++a_stacks;
    for (const ChainLink &link : merged.links) {
      if (const auto resolved = ResolvePlacement(a_state, link.placement)) {
        a_links.insert({resolved->placement, link.output});
      }
    }
  }
}

std::vector<bool> DrawnByMergedChains(const LiveActor &a_state) {
  std::set<MergedLink> links;
  std::size_t mergedStacks = 0;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      AddMergedLinks(a_state, geometry, links, mergedStacks);
    }
  }
  std::vector<bool> covered;
  for (std::size_t p = 0; p < a_state.placements.size(); ++p) {
    const std::vector<PlacedOutput> &outputs = a_state.placements[p].outputs;
    for (std::size_t o = 0; o < outputs.size(); ++o) {
      if (outputs[o].stack) {
        covered.push_back(links.contains({p, o}));
      }
    }
  }
  covered.insert(covered.end(), mergedStacks, false);
  return covered;
}

std::optional<HiddenSlot> HiddenSlotOf(const LiveActor &a_state,
                                       const GeometrySlot &a_slot) {
  std::size_t flat = 0;
  for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
    const std::size_t geometries = a_state.pieces[p].geometries.size();
    const std::size_t index = IndexOf(a_slot.geometry);
    if (index < flat + geometries) {
      return HiddenSlot{p, index - flat, a_slot.slot, false};
    }
    flat += geometries;
  }
  return std::nullopt;
}

void ConcealStackSlots(LiveGeometry &a_geometry) {
  for (const SlotStackPlan &slot : a_geometry.stackPlan.slots) {
    if (SlotTarget *target = TargetFor(a_geometry, slot.surface)) {
      target->WriteTexture(slot.slot, nullptr);
    }
  }
}

void ConcealStackSlots(LiveActor &a_state) {
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      ConcealStackSlots(geometry);
    }
  }
}

bool StacksMatch(const std::vector<StackHolder> &a_holders,
                 const std::vector<ResizedStack> &a_resized) {
  if (a_holders.size() != a_resized.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_holders.size(); ++i) {
    const RenderOutput *current = a_holders[i]->get();
    const RenderOutput *resized = a_resized[i].stack.get();
    if (!current || !resized || !(current->Result() == resized->Result())) {
      return false;
    }
  }
  return true;
}

void UpdateResizeInputs(RenderInstance &a_render, const LiveActor &a_state,
                        WarningHistory &a_warnings) {
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    const LiveInstance &instance = a_state.instances[i];
    if (!instance.graph || !instance.signals) {
      continue;
    }
    if (const auto updated =
            a_render.Update(*instance.graph, i + 1, *instance.signals);
        !updated) {
      LogOnce(a_warnings, Severity::kError,
              std::format("render inputs of a resized instance: {}",
                          updated.error()));
    }
  }
}

void SwapRender(LiveActor &a_state,
                const std::shared_ptr<RenderInstance> &a_from,
                const std::shared_ptr<RenderInstance> &a_to) {
  if (!a_from) {
    return;
  }
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render == a_from) {
        geometry.inputs.render = a_to;
      }
    }
  }
}
}

std::shared_ptr<RenderInstance> AttachedRender(const LiveActor &a_state) {
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render) {
        return geometry.inputs.render;
      }
    }
  }
  return nullptr;
}

DrawPace DrawResize(LiveActor &a_state, std::uint64_t a_frame,
                    std::uint64_t a_nowMS, WarningHistory &a_warnings,
                    DrawPace a_pace, std::uint64_t a_budgetUs) {
  ResizeInFlight *inFlight = a_state.resize ? &*a_state.resize : nullptr;
  TextureLab *lab = TextureLab::GetSingleton();
  Compositor *compositor = Compositor::GetSingleton();
  if (!inFlight || !inFlight->render || !lab || !compositor ||
      !MayDrawStack(a_pace, a_budgetUs)) {
    return a_pace;
  }
  const TextureLab::PassBatch batch{*lab};
  if (inFlight->render->BeginFrame(a_frame, a_nowMS)) {
    UpdateResizeInputs(*inFlight->render, a_state, a_warnings);
  }
  for (ResizedStack &resized : inFlight->stacks) {
    if (resized.drawn) {
      continue;
    }
    if (!MayDrawStack(a_pace, a_budgetUs)) {
      break;
    }
    const Metrics::Stopwatch watch;
    resized.drawn = !resized.stack ||
                    compositor->Render(*resized.stack, LayerFilter{},
                                       StackBase{}) != StackRender::kPending;
    a_pace = Drew(a_pace, watch.Micros());
  }
  return a_pace;
}

const RenderInstance *ResizingInstance(const LiveActor &a_state) noexcept {
  return a_state.resize ? a_state.resize->render.get() : nullptr;
}

std::expected<std::size_t, std::string> StartResize(LiveActor &a_state,
                                                    ResolutionLevel a_to) {
  const std::shared_ptr<RenderInstance> current = AttachedRender(a_state);
  if (!current) {
    return std::unexpected(std::string{"no render to resize"});
  }
  std::expected<PlanSizes, std::string> sizes =
      SizesFor(current->Plan(), AtLevel(a_state.fullSizes, a_to).sizes);
  if (!sizes) {
    return std::unexpected(std::move(sizes.error()));
  }
  ResizeInFlight inFlight{a_to, current->AtSizes(std::move(*sizes)), {}};
  const std::vector<StackHolder> holders = StackHolders(a_state);
  const std::vector<bool> covered = DrawnByMergedChains(a_state);
  for (std::size_t i = 0; i < holders.size(); ++i) {
    std::unique_ptr<RenderOutput> stack =
        (*holders[i])->DrawnBy(inFlight.render);
    if (!stack) {
      return std::unexpected(
          std::string{"a stack output names no stack of the plan"});
    }
    inFlight.stacks.push_back(
        {std::move(stack), i < covered.size() && covered[i]});
  }
  const std::size_t stacks = inFlight.stacks.size();
  a_state.resize = std::move(inFlight);
  return stacks;
}

std::expected<ResizeSwap, std::string> FinishResize(LiveActor &a_state) {
  ResizeInFlight *inFlight = a_state.resize ? &*a_state.resize : nullptr;
  const std::shared_ptr<RenderInstance> current = AttachedRender(a_state);
  if (!inFlight || !inFlight->render || !current) {
    return std::unexpected(std::string{"no resize to swap"});
  }
  const std::vector<StackHolder> holders = StackHolders(a_state);
  if (!StacksMatch(holders, inFlight->stacks)) {
    return std::unexpected(
        std::string{"the stacks changed while the resize drew"});
  }
  for (std::size_t i = 0; i < holders.size(); ++i) {
    *holders[i] = std::move(inFlight->stacks[i].stack);
  }
  SwapRender(a_state, current, inFlight->render);
  ConcealStackSlots(a_state);
  const ResizeSwap swap{a_state.renderLevel, inFlight->to,
                        inFlight->stacks.size(), DrawnStacks(*inFlight),
                        CostOf(inFlight->render->Targets().withIntermediates)};
  a_state.renderLevel = inFlight->to;
  a_state.holding = IntermediateHolding::kHeld;
  DropResize(a_state);
  return swap;
}

void DropResize(LiveActor &a_state) { a_state.resize.reset(); }

const ResizeInFlight *InFlightOf(const LiveActor &a_state) noexcept {
  return a_state.resize ? &*a_state.resize : nullptr;
}

std::size_t DrawnStacks(const ResizeInFlight &a_inFlight) noexcept {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_inFlight.stacks, [](const ResizedStack &a_stack) {
        return a_stack.drawn;
      }));
}

void StartFirstDraw(LiveActor &a_state, std::span<const GeometrySlot> a_slots) {
  a_state.firstDraw.reset();
  if (!AttachedRender(a_state)) {
    return;
  }
  FirstDraw draw;
  for (const GeometrySlot &slot : a_slots) {
    if (const std::optional<HiddenSlot> hidden = HiddenSlotOf(a_state, slot)) {
      draw.slots.push_back(*hidden);
    }
  }
  a_state.firstDraw = std::move(draw);
}

std::size_t DrawnSlots(const FirstDraw &a_draw) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      a_draw.slots, [](const HiddenSlot &a_slot) { return a_slot.drawn; }));
}

LiveGeometry *GeometryOf(LiveActor &a_state,
                         const HiddenSlot &a_slot) noexcept {
  if (a_slot.piece >= a_state.pieces.size()) {
    return nullptr;
  }
  LivePiece &piece = a_state.pieces[a_slot.piece];
  return a_slot.geometry < piece.geometries.size()
             ? &piece.geometries[a_slot.geometry]
             : nullptr;
}
}
