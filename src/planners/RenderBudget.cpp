// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderBudget.h"

#include <algorithm>
#include <format>
#include <limits>

namespace BetterEnchantmentEffects {
namespace {
std::uint64_t Sum(std::uint64_t a_lhs, std::uint64_t a_rhs) noexcept {
  return a_lhs > std::numeric_limits<std::uint64_t>::max() - a_rhs
             ? std::numeric_limits<std::uint64_t>::max()
             : a_lhs + a_rhs;
}

std::uint64_t Less(std::uint64_t a_lhs, std::uint64_t a_rhs) noexcept {
  return a_lhs > a_rhs ? a_lhs - a_rhs : 0;
}

bool BetterStepDown(const BudgetActor &a_candidate,
                    const BudgetActor &a_chosen) noexcept {
  const int candidateGap =
      LevelGap(a_candidate.level, IdealLevel(a_candidate.screenFraction));
  const int chosenGap =
      LevelGap(a_chosen.level, IdealLevel(a_chosen.screenFraction));
  if (candidateGap != chosenGap) {
    return candidateGap > chosenGap;
  }
  return a_candidate.stepDown->freed > a_chosen.stepDown->freed;
}

bool CanStepDown(const BudgetActor &a_actor) noexcept {
  return a_actor.role != ActorRole::kPlayer && !a_actor.resizing &&
         a_actor.stepDown.has_value();
}

std::optional<std::size_t>
StepDownCandidate(std::span<const BudgetActor> a_actors) noexcept {
  std::optional<std::size_t> chosen;
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    if (CanStepDown(a_actors[i]) &&
        (!chosen || BetterStepDown(a_actors[i], a_actors[*chosen]))) {
      chosen = i;
    }
  }
  return chosen;
}

std::uint64_t FreeVideoMemory(const VideoMemory &a_memory) noexcept {
  return Less(a_memory.budget, a_memory.usage);
}

std::vector<std::size_t> ReleaseOrder(std::span<const BudgetActor> a_actors) {
  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    if (!a_actors[i].inView && !a_actors[i].resizing &&
        a_actors[i].releasable > 0) {
      order.push_back(i);
    }
  }
  std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) {
    return a_actors[a_lhs].releasable > a_actors[a_rhs].releasable;
  });
  return order;
}

std::string HolderText(const ActorHolding &a_holding) {
  return a_holding.name.empty()
             ? std::format("{:08X} {}", a_holding.actor,
                           Mebibytes(a_holding.ownBytes))
             : std::format("{} ({:08X}) {}", a_holding.name, a_holding.actor,
                           Mebibytes(a_holding.ownBytes));
}

std::string AllowanceText(const BudgetState &a_state) {
  return a_state.allowance ? Mebibytes(*a_state.allowance)
                           : std::string{"unknown"};
}
}

std::uint64_t MarginOf(std::uint64_t a_budget) noexcept {
  return std::max(kMinBudgetMargin, a_budget / kBudgetMarginDivisor);
}

std::uint64_t BandOf(std::uint64_t a_allowance) noexcept {
  return std::min(
      std::max(kMinPressureBand, a_allowance / kPressureBandDivisor),
      a_allowance / 2);
}

std::optional<std::uint64_t>
AllowanceOf(const BudgetSample &a_sample) noexcept {
  if (!a_sample.memory || a_sample.memory->budget == 0) {
    return std::nullopt;
  }
  const VideoMemory &memory = *a_sample.memory;
  const std::uint64_t plugin = std::min(a_sample.pluginBytes, memory.usage);
  const std::uint64_t allowance =
      Less(Sum(plugin, FreeVideoMemory(memory)), MarginOf(memory.budget));
  return a_sample.capBytes > 0 ? std::min(allowance, a_sample.capBytes)
                               : allowance;
}

bool IsOver(Pressure a_pressure) noexcept {
  return a_pressure != Pressure::kNone;
}

std::uint64_t PressureGoal(std::uint64_t a_allowance) noexcept {
  return Less(a_allowance, BandOf(a_allowance));
}

BudgetState NextBudget(const BudgetState &a_current,
                       std::optional<std::uint64_t> a_allowance,
                       std::uint64_t a_planned) noexcept {
  if (!a_allowance) {
    return {std::nullopt, a_planned, Pressure::kNone};
  }
  if (IsOver(a_current.pressure)) {
    const bool ends = a_planned < PressureGoal(*a_allowance);
    return {a_allowance, a_planned,
            ends ? Pressure::kNone : a_current.pressure};
  }
  return {a_allowance, a_planned,
          a_planned > *a_allowance ? Pressure::kOver : Pressure::kNone};
}

Admission AdmitGrowth(const BudgetState &a_state,
                      std::uint64_t a_added) noexcept {
  if (!a_state.allowance) {
    return Admission::kAdmitted;
  }
  if (IsOver(a_state.pressure)) {
    return Admission::kUnderPressure;
  }
  return Sum(a_state.planned, a_added) > *a_state.allowance
             ? Admission::kOverAllowance
             : Admission::kAdmitted;
}

BudgetState Charged(BudgetState a_state, std::uint64_t a_added) noexcept {
  a_state.planned = Sum(a_state.planned, a_added);
  return a_state;
}

BudgetState Released(BudgetState a_state, std::uint64_t a_freed) noexcept {
  a_state.planned = Less(a_state.planned, a_freed);
  return a_state;
}

std::optional<std::uint64_t> HeadroomOf(const BudgetState &a_state,
                                        std::uint64_t a_replaced) noexcept {
  if (!a_state.allowance) {
    return std::nullopt;
  }
  if (IsOver(a_state.pressure)) {
    return a_replaced;
  }
  return Less(*a_state.allowance, Less(a_state.planned, a_replaced));
}

ResolutionLevel FittingLevel(ResolutionLevel a_finest,
                             const LevelBytes &a_bytes,
                             std::optional<std::uint64_t> a_headroom) noexcept {
  if (!a_headroom) {
    return a_finest;
  }
  for (std::optional<ResolutionLevel> level = a_finest; level;
       level = CoarserLevel(*level)) {
    const std::optional<std::uint64_t> &bytes = a_bytes.At(*level);
    if (bytes && *bytes <= *a_headroom) {
      return *level;
    }
  }
  return ResolutionLevel::kEighth;
}

std::uint64_t PlannedBytes(const LevelCost &a_cost,
                           IntermediateHolding a_holding) noexcept {
  return a_holding == IntermediateHolding::kReleased ? a_cost.stacksOnly
                                                     : a_cost.withIntermediates;
}

std::uint64_t IntermediateBytes(const LevelCost &a_cost) noexcept {
  return Less(a_cost.withIntermediates, a_cost.stacksOnly);
}

std::optional<StepDownOption>
StepDownOf(ResolutionLevel a_level, const LevelCost &a_current,
           const std::optional<LevelCost> &a_coarser,
           IntermediateHolding a_holding) noexcept {
  const std::optional<ResolutionLevel> coarser = CoarserLevel(a_level);
  if (!coarser || !a_coarser) {
    return std::nullopt;
  }
  return StepDownOption{*coarser, Less(PlannedBytes(a_current, a_holding),
                                       PlannedBytes(*a_coarser, a_holding))};
}

BudgetResponse RespondToPressure(std::uint64_t a_planned, std::uint64_t a_goal,
                                 std::span<const BudgetActor> a_actors,
                                 bool a_resizeBusy) {
  BudgetResponse response{{}, Enough{}};
  std::uint64_t remaining = a_planned;
  if (remaining < a_goal) {
    return response;
  }
  for (const std::size_t index : ReleaseOrder(a_actors)) {
    if (remaining < a_goal) {
      break;
    }
    const BudgetActor &actor = a_actors[index];
    response.release.push_back({actor.actor, actor.releasable});
    remaining = Less(remaining, actor.releasable);
  }
  if (remaining < a_goal) {
    return response;
  }
  if (a_resizeBusy) {
    response.next = AwaitResize{};
    return response;
  }
  if (const std::optional<std::size_t> chosen = StepDownCandidate(a_actors)) {
    const BudgetActor &actor = a_actors[*chosen];
    response.next = StepDown{actor.actor, *actor.stepDown};
    return response;
  }
  response.next = Exhausted{};
  return response;
}

std::string_view AdmissionName(Admission a_admission) noexcept {
  switch (a_admission) {
  case Admission::kAdmitted:
    return "admitted";
  case Admission::kUnderPressure:
    return "pressure";
  case Admission::kOverAllowance:
    return "over allowance";
  }
  return "admitted";
}

std::string PressureStartLine(const BudgetSample &a_sample,
                              const BudgetState &a_state) {
  const std::string memory =
      a_sample.memory ? std::format("DXGI budget {}, usage {}",
                                    Mebibytes(a_sample.memory->budget),
                                    Mebibytes(a_sample.memory->usage))
                      : std::string{"DXGI memory unknown"};
  const std::string cap =
      a_sample.capBytes > 0
          ? std::format("cap {}", Mebibytes(a_sample.capBytes))
          : std::string{"no cap"};
  return std::format("render target budget: pressure starts: planned {} is "
                     "over the allowance {} ({}, plugin targets {}, {})",
                     Mebibytes(a_state.planned), AllowanceText(a_state), memory,
                     Mebibytes(a_sample.pluginBytes), cap);
}

std::string PressureEndLine(const BudgetState &a_state) {
  const std::uint64_t allowance = a_state.allowance.value_or(0);
  return std::format("render target budget: pressure ends: planned {} is "
                     "under the allowance {} less the band {}",
                     Mebibytes(a_state.planned), AllowanceText(a_state),
                     Mebibytes(BandOf(allowance)));
}

std::string StepDownLine(std::uint32_t a_actor, std::string_view a_name,
                         ResolutionLevel a_from,
                         const StepDownOption &a_option) {
  return std::format("actor {:08X} ({}) steps down from {} to {} resolution: "
                     "frees {} (render target budget)",
                     a_actor, a_name.empty() ? "unnamed" : a_name,
                     LevelName(a_from), LevelName(a_option.to),
                     Mebibytes(a_option.freed));
}

std::string ExhaustedLine(const BudgetState &a_state,
                          std::span<const ActorHolding> a_largest) {
  std::string holders;
  for (const ActorHolding &holding : a_largest) {
    holders += (holders.empty() ? "" : "; ") + HolderText(holding);
  }
  return std::format("render target budget: no release or step down is "
                     "left: planned {} against the allowance {}; the largest "
                     "holdings: {}",
                     Mebibytes(a_state.planned), AllowanceText(a_state),
                     holders.empty() ? std::string{"none"} : holders);
}
}
