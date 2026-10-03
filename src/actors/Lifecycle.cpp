// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"

#include "actors/Reconcile.h"

#include <algorithm>
#include <optional>

namespace BetterEnchantmentEffects::Actors {
namespace {
const ActorPlan &InstalledOf(const Phase &a_phase) {
  static const ActorPlan none;
  const Present *present = std::get_if<Present>(&a_phase);
  return present ? present->installed : none;
}

std::optional<ModelWait> WaitOf(const Phase &a_phase) {
  const Present *present = std::get_if<Present>(&a_phase);
  return present ? present->wait : std::nullopt;
}

ModelWait Waited(std::optional<ModelWait> a_wait, std::uint32_t a_nowMs) {
  ModelWait wait = a_wait.value_or(ModelWait{a_nowMs, false});
  wait.expired = wait.expired || a_nowMs - wait.sinceMs >= kModelWaitMs;
  return wait;
}

Advanced Retiring(Phase a_next, const ActorPlan &a_installed) {
  return {std::move(a_next), Reconcile(a_installed, ActorPlan{})};
}
}

bool AwaitsModel(const ActorFacts &a_facts) noexcept {
  return std::ranges::any_of(
      a_facts.pieces, [](const PieceFacts &a_piece) { return !a_piece.model; });
}

Advanced Advance(const AdvanceRequest &a_request) {
  const ActorPlan &installed = InstalledOf(a_request.phase);
  if (!a_request.facts.loaded) {
    return Retiring(Unloaded{}, installed);
  }
  if (!a_request.facts.inRange) {
    return Retiring(Evicted{}, installed);
  }
  if (!AwaitsModel(a_request.facts)) {
    return {Present{installed, std::nullopt},
            Reconcile(installed, a_request.plan)};
  }
  if (installed == ActorPlan{}) {
    return {Present{installed, std::nullopt},
            Reconcile(installed, a_request.plan)};
  }
  const ModelWait wait = Waited(WaitOf(a_request.phase), a_request.nowMs);
  if (!wait.expired) {
    return {Present{installed, wait}, {}};
  }
  return {Present{installed, wait}, Reconcile(installed, a_request.plan)};
}
}
