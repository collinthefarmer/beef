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

void RequestResize(LiveActor &a_state, ResolutionLevel a_level) {
  a_state.resize = ResizePending{a_level};
}

std::expected<void, std::string> StartResize(LiveActor &a_state,
                                             std::uint64_t a_frame) {
  const ResizePending *pending = Get<ResizePending>(a_state.resize);
  if (!pending) {
    return {};
  }
  ResizeInFlight inFlight{{a_state.appliedResolution, pending->level, a_frame},
                          pending->level,
                          nullptr,
                          {}};
  const std::shared_ptr<RenderInstance> current = AttachedRender(a_state);
  if (current) {
    std::expected<PlanSizes, std::string> sizes =
        SizesFor(current->Plan(),
                 AtLevel(a_state.fullSizes, inFlight.progress.to).sizes);
    if (!sizes) {
      a_state.resize = ResizeIdle{};
      return std::unexpected(sizes.error());
    }
    inFlight.render = current->AtSizes(std::move(*sizes));
    const std::vector<StackHolder> holders = StackHolders(a_state);
    const std::vector<bool> covered = DrawnByMergedChains(a_state);
    for (std::size_t i = 0; i < holders.size(); ++i) {
      std::unique_ptr<RenderOutput> stack =
          (*holders[i])->DrawnBy(inFlight.render);
      if (!stack) {
        a_state.resize = ResizeIdle{};
        return std::unexpected("a stack output names no stack of the plan");
      }
      inFlight.stacks.push_back(
          {std::move(stack), i < covered.size() && covered[i]});
    }
  }
  a_state.resize = std::move(inFlight);
  return {};
}

DrawPace DrawResize(LiveActor &a_state, std::uint64_t a_frame,
                    std::uint64_t a_nowMS, WarningHistory &a_warnings,
                    DrawPace a_pace, std::uint64_t a_budgetUs) {
  ResizeInFlight *inFlight = Get<ResizeInFlight>(a_state.resize);
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

std::optional<ResizeSwap> FinishResize(LiveActor &a_state,
                                       std::uint64_t a_frame) {
  ResizeInFlight *inFlight = Get<ResizeInFlight>(a_state.resize);
  if (!inFlight) {
    return std::nullopt;
  }
  const std::size_t drawn = DrawnStacks(*inFlight);
  if (!SwapDue(inFlight->progress,
               DrawOf({.inView = a_state.inView,
                       .drawn = drawn,
                       .stacks = inFlight->stacks.size()}),
               a_frame)) {
    return std::nullopt;
  }
  const std::vector<StackHolder> holders = StackHolders(a_state);
  const std::shared_ptr<RenderInstance> current = AttachedRender(a_state);
  if (!StacksMatch(holders, inFlight->stacks) ||
      static_cast<bool>(current) != static_cast<bool>(inFlight->render)) {
    a_state.resize = ResizePending{inFlight->progress.to};
    return std::nullopt;
  }
  for (std::size_t i = 0; i < holders.size(); ++i) {
    *holders[i] = std::move(inFlight->stacks[i].stack);
  }
  SwapRender(a_state, current, inFlight->render);
  if (!a_state.inView) {
    ConcealStackSlots(a_state);
  }
  const ResizeSwap swap{
      inFlight->progress.from,
      inFlight->progress.to,
      a_frame - std::min(a_frame, inFlight->progress.beganFrame),
      inFlight->stacks.size(),
      drawn,
      inFlight->render ? CostOf(inFlight->render->Targets().withIntermediates)
                       : TargetCost{}};
  SetAppliedResolution(a_state, inFlight->progress.to);
  a_state.resize = ResizeIdle{};
  return swap;
}

const RenderInstance *ResizingInstance(const LiveActor &a_state) noexcept {
  const ResizeInFlight *inFlight = Get<ResizeInFlight>(a_state.resize);
  return inFlight ? inFlight->render.get() : nullptr;
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
  ResizeInFlight inFlight{{a_state.renderLevel, a_to, 0},
                          a_to,
                          current->AtSizes(std::move(*sizes)),
                          {}};
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
  ResizeInFlight *inFlight = Get<ResizeInFlight>(a_state.resize);
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
  const ResizeSwap swap{a_state.renderLevel,
                        inFlight->to,
                        0,
                        inFlight->stacks.size(),
                        DrawnStacks(*inFlight),
                        CostOf(inFlight->render->Targets().withIntermediates)};
  a_state.renderLevel = inFlight->to;
  a_state.holding = IntermediateHolding::kHeld;
  DropResize(a_state);
  return swap;
}

void DropResize(LiveActor &a_state) { a_state.resize = ResizeIdle{}; }

const ResizeInFlight *InFlightOf(const LiveActor &a_state) noexcept {
  return Get<ResizeInFlight>(a_state.resize);
}

std::size_t DrawnStacks(const ResizeInFlight &a_inFlight) noexcept {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_inFlight.stacks, [](const ResizedStack &a_stack) {
        return a_stack.drawn;
      }));
}

void StartFirstDraw(LiveActor &a_state) { StartFirstDraw(a_state, 0); }

void StartFirstDraw(LiveActor &a_state, std::uint64_t a_frame) {
  a_state.firstDraw.reset();
  if (!AttachedRender(a_state)) {
    return;
  }
  FirstDraw draw{a_frame, 0, {}};
  for (std::size_t p = 0; p < a_state.pieces.size(); ++p) {
    const LivePiece &piece = a_state.pieces[p];
    for (std::size_t g = 0; g < piece.geometries.size(); ++g) {
      const std::size_t slots = piece.geometries[g].stackPlan.slots.size();
      for (std::size_t s = 0; s < slots; ++s) {
        draw.slots.push_back({p, g, s, false});
      }
    }
  }
  a_state.firstDraw = std::move(draw);
}

FirstDrawProgress ProgressOf(const FirstDraw &a_draw) noexcept {
  return {a_draw.drawTicks, a_draw.slots.size(),
          static_cast<std::size_t>(
              std::ranges::count_if(a_draw.slots, [](const HiddenSlot &a_slot) {
                return a_slot.drawn;
              }))};
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
