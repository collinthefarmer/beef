// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"

#include "Core.h"
#include "actors/Reconcile.h"
#include "planners/Eviction.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace BetterEnchantmentEffects::Actors {
namespace {
const ActorPlan &InstalledOf(const std::optional<ActorState> &a_state) {
  static const ActorPlan none;
  return a_state ? a_state->installed : none;
}

std::optional<std::uint32_t>
PendingSince(const std::optional<ActorState> &a_state,
             const PieceRef &a_piece) {
  if (!a_state) {
    return std::nullopt;
  }
  for (const PendingModel &model : a_state->pending) {
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
           PendingSince(a_request.state, piece.ref).value_or(a_request.nowMs)});
    }
  }
  return pending;
}

bool Waiting(std::span<const PendingModel> a_pending, std::uint32_t a_nowMs) {
  return std::ranges::any_of(a_pending, [&](const PendingModel &a_model) {
    return !a_model.piece.firstPerson &&
           a_nowMs - a_model.sinceMs < kModelWaitMs;
  });
}

std::vector<ActorCommand> CommandsFor(Reinstall a_reinstall,
                                      const ActorPlan &a_installed,
                                      const ActorPlan &a_plan) {
  if (a_reinstall == Reinstall::kIfChanged) {
    return Reconcile(a_installed, a_plan);
  }
  std::vector<ActorCommand> commands = Reconcile(a_installed, ActorPlan{});
  std::ranges::move(Reconcile(ActorPlan{}, a_plan),
                    std::back_inserter(commands));
  return commands;
}

std::optional<ActorState> Held(ActorState a_state) {
  const bool empty = a_state.plan == ActorPlan{} &&
                     a_state.installed == ActorPlan{} &&
                     a_state.pending.empty() && a_state.claims.empty();
  if (empty) {
    return std::nullopt;
  }
  return a_state;
}
}

Transition Advance(const AdvanceRequest &a_request) {
  const ActorPlan &installed = InstalledOf(a_request.state);
  if (!a_request.facts.loaded) {
    return {std::nullopt, Reconcile(installed, ActorPlan{})};
  }
  std::vector<Claim> claims =
      a_request.state ? LiveClaims(a_request.state->claims, a_request.facts)
                      : std::vector<Claim>{};
  const bool hasInstalled = !(installed == ActorPlan{});
  if (!InRange(hasInstalled, a_request.facts.distance,
               a_request.evictDistance)) {
    return {Held({a_request.plan, ActorPlan{}, {}, std::move(claims)}),
            Reconcile(installed, ActorPlan{})};
  }
  std::vector<PendingModel> pending = PendingOf(a_request);
  const bool wait = hasInstalled &&
                    a_request.reinstall == Reinstall::kIfChanged &&
                    Waiting(pending, a_request.nowMs);
  std::vector<ActorCommand> commands =
      wait ? std::vector<ActorCommand>{}
           : CommandsFor(a_request.reinstall, installed, a_request.plan);
  ActorPlan after = AfterCommands(installed, commands);
  return {Held({a_request.plan, std::move(after), std::move(pending),
                std::move(claims)}),
          std::move(commands)};
}

bool InRange(bool a_installed, std::optional<float> a_distance,
             float a_evictDistance) {
  if (!a_distance) {
    return true;
  }
  return a_installed ? EvictionFor(*a_distance, a_evictDistance, true) !=
                           EvictionAction::kEvict
                     : Returns(*a_distance, a_evictDistance);
}

bool AnythingInstalled(const std::optional<ActorState> &a_state) noexcept {
  return a_state && !(a_state->installed == ActorPlan{});
}

ModelLoad ModelLoadOf(const std::optional<ActorState> &a_state,
                      std::uint32_t a_nowMs) {
  if (!a_state || a_state->pending.empty()) {
    return ModelLoad::kNone;
  }
  const bool waiting =
      std::ranges::any_of(a_state->pending, [&](const PendingModel &a_model) {
        return a_nowMs - a_model.sinceMs < kModelWaitMs + kLoadingCheckMs;
      });
  return waiting ? ModelLoad::kLoading : ModelLoad::kLate;
}

std::vector<Claim> LiveClaims(std::span<const Claim> a_claims,
                              const ActorFacts &a_facts) {
  std::vector<Claim> live;
  for (const Claim &claim : a_claims) {
    const bool current =
        std::ranges::any_of(a_facts.pieces, [&](const PieceFacts &a_piece) {
          return a_piece.ref == claim.geometry.piece &&
                 a_piece.model == claim.model;
        });
    if (current) {
      live.push_back(claim);
    }
  }
  return live;
}

ActorState WithClaim(ActorState a_state, Claim a_claim) {
  if (!std::ranges::contains(a_state.claims, a_claim)) {
    a_state.claims.push_back(std::move(a_claim));
  }
  return a_state;
}
}
