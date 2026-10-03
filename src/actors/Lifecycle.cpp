// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"

#include "actors/Reconcile.h"
#include "planners/Eviction.h"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
namespace {
const ActorPlan &InstalledOf(const Phase &a_phase) {
  static const ActorPlan none;
  const Present *present = std::get_if<Present>(&a_phase);
  return present ? present->installed : none;
}

bool InRange(const AdvanceRequest &a_request) {
  if (!a_request.facts.distance) {
    return true;
  }
  const bool evicted = std::holds_alternative<Evicted>(a_request.phase);
  const EvictionAction action =
      EvictionFor(*a_request.facts.distance, a_request.evictDistance, !evicted);
  return evicted ? action == EvictionAction::kRestore ||
                       a_request.evictDistance <= 0.0f
                 : action != EvictionAction::kEvict;
}

std::optional<std::uint32_t> PendingSince(const Phase &a_phase,
                                          const PieceRef &a_piece) {
  const Present *present = std::get_if<Present>(&a_phase);
  if (!present) {
    return std::nullopt;
  }
  for (const PendingModel &model : present->pending) {
    if (model.piece == a_piece) {
      return model.sinceMs;
    }
  }
  return std::nullopt;
}

std::vector<PendingModel> PendingOf(const AdvanceRequest &a_request) {
  std::vector<PendingModel> pending;
  for (const PieceFacts &piece : a_request.facts.pieces) {
    if (!piece.model) {
      pending.push_back(
          {piece.ref,
           PendingSince(a_request.phase, piece.ref).value_or(a_request.nowMs)});
    }
  }
  return pending;
}

bool Waiting(std::span<const PendingModel> a_pending, std::uint32_t a_nowMs) {
  return std::ranges::any_of(a_pending, [&](const PendingModel &a_model) {
    return a_nowMs - a_model.sinceMs < kModelWaitMs;
  });
}

Advanced Retiring(Phase a_next, const ActorPlan &a_installed) {
  return {std::move(a_next), Reconcile(a_installed, ActorPlan{})};
}
}

Advanced Advance(const AdvanceRequest &a_request) {
  const ActorPlan &installed = InstalledOf(a_request.phase);
  if (!a_request.facts.loaded) {
    return Retiring(Unloaded{}, installed);
  }
  if (!InRange(a_request)) {
    return Retiring(Evicted{}, installed);
  }
  std::vector<PendingModel> pending = PendingOf(a_request);
  const bool wait =
      !(installed == ActorPlan{}) && Waiting(pending, a_request.nowMs);
  std::vector<ActorCommand> commands =
      wait ? std::vector<ActorCommand>{} : Reconcile(installed, a_request.plan);
  return {Present{installed, std::move(pending)}, std::move(commands)};
}
}
