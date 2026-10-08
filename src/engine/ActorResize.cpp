// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorResize.h"

#include "engine/LogOnce.h"
#include "render/Compositor.h"
#include "render/RenderInstance.h"
#include "render/TextureLab.h"

#include <algorithm>
#include <format>
#include <memory>
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

std::shared_ptr<RenderInstance> CurrentRender(const LiveActor &a_state) {
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render) {
        return geometry.inputs.render;
      }
    }
  }
  return nullptr;
}

bool StacksMatch(const std::vector<StackHolder> &a_holders,
                 const std::vector<ResizedStack> &a_built) {
  if (a_holders.size() != a_built.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_holders.size(); ++i) {
    const RenderOutput *current = a_holders[i]->get();
    const RenderOutput *built = a_built[i].stack.get();
    if (!current || !built || !(current->Result() == built->Result())) {
      return false;
    }
  }
  return true;
}

std::size_t DrawnCount(const ResizeBuild &a_build) {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_build.stacks, [](const ResizedStack &a_stack) {
        return a_stack.drawn;
      }));
}

void UpdateBuildInputs(RenderInstance &a_render, const LiveActor &a_state,
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

void AttachBuild(LiveActor &a_state,
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

void RequestResize(LiveActor &a_state, ResolutionLevel a_level) {
  a_state.resize = ResizePending{a_level};
}

std::expected<void, std::string> StartResize(LiveActor &a_state,
                                             std::uint64_t a_frame) {
  const ResizePending *pending = Get<ResizePending>(a_state.resize);
  if (!pending) {
    return {};
  }
  ResizeBuild build{{a_state.resolution, pending->level, a_frame}, nullptr, {}};
  const std::shared_ptr<RenderInstance> current = CurrentRender(a_state);
  if (current) {
    std::expected<PlanSides, std::string> sides = SidesFor(
        current->Plan(), AtLevel(a_state.fullSides, build.progress.to).pixels);
    if (!sides) {
      a_state.resize = ResizeIdle{};
      return std::unexpected(sides.error());
    }
    build.render = current->AtSides(std::move(*sides));
    for (const StackHolder holder : StackHolders(a_state)) {
      std::unique_ptr<RenderOutput> stack = (*holder)->DrawnBy(build.render);
      if (!stack) {
        a_state.resize = ResizeIdle{};
        return std::unexpected("a stack output names no stack of the plan");
      }
      build.stacks.push_back({std::move(stack), false});
    }
  }
  a_state.resize = std::move(build);
  return {};
}

void DrawResize(LiveActor &a_state, std::uint64_t a_frame,
                std::uint64_t a_nowMS, WarningHistory &a_warnings) {
  ResizeBuild *build = Get<ResizeBuild>(a_state.resize);
  TextureLab *lab = TextureLab::GetSingleton();
  Compositor *compositor = Compositor::GetSingleton();
  if (!build || !build->render || !lab || !compositor) {
    return;
  }
  const TextureLab::PassBatch batch{*lab};
  if (build->render->BeginFrame(a_frame, a_nowMS)) {
    UpdateBuildInputs(*build->render, a_state, a_warnings);
  }
  for (ResizedStack &built : build->stacks) {
    if (built.drawn) {
      continue;
    }
    built.drawn = !built.stack ||
                  compositor->Render(*built.stack, LayerFilter{},
                                     StackBase{}) != StackRender::kPending;
  }
}

std::optional<ResizeSwap> FinishResize(LiveActor &a_state,
                                       std::uint64_t a_frame) {
  ResizeBuild *build = Get<ResizeBuild>(a_state.resize);
  if (!build) {
    return std::nullopt;
  }
  const std::size_t drawn = DrawnCount(*build);
  if (!SwapDue(build->progress,
               DrawOf(a_state.inView, drawn, build->stacks.size()), a_frame)) {
    return std::nullopt;
  }
  const std::vector<StackHolder> holders = StackHolders(a_state);
  const std::shared_ptr<RenderInstance> current = CurrentRender(a_state);
  if (!StacksMatch(holders, build->stacks) ||
      static_cast<bool>(current) != static_cast<bool>(build->render)) {
    a_state.resize = ResizePending{build->progress.to};
    return std::nullopt;
  }
  for (std::size_t i = 0; i < holders.size(); ++i) {
    *holders[i] = std::move(build->stacks[i].stack);
  }
  AttachBuild(a_state, current, build->render);
  const ResizeSwap swap{build->progress.from,
                        build->progress.to,
                        a_frame - std::min(a_frame, build->progress.beganFrame),
                        build->stacks.size(),
                        drawn,
                        build->render ? CostOf(build->render->Targets().held)
                                      : TargetCost{}};
  a_state.resolution = build->progress.to;
  a_state.resize = ResizeIdle{};
  return swap;
}

const RenderInstance *ResizingInstance(const LiveActor &a_state) noexcept {
  const ResizeBuild *build = Get<ResizeBuild>(a_state.resize);
  return build ? build->render.get() : nullptr;
}
}
