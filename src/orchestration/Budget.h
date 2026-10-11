// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Facts.h"
#include "orchestration/World.h"
#include "planners/BudgetRadius.h"
#include "planners/RenderBudget.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct BudgetActors {
  std::vector<BudgetActor> actors;
  bool resizeBusy = false;
};

struct WaitingStepUp {
  ActorId actor{};
  PreemptionRequest request;
};

[[nodiscard]] std::uint64_t
PlannedBytesOf(const TrackedActor &a_record) noexcept;
[[nodiscard]] std::uint64_t PlannedBytesOf(const World &a_world) noexcept;
[[nodiscard]] InstallBounds
InstallBoundsOf(const TrackedActor &a_record, const Sighting &a_sight,
                const BudgetState &a_budget, std::uint64_t a_replaced,
                const TickSettings &a_settings) noexcept;
[[nodiscard]] std::optional<BudgetActor>
BudgetActorOf(ActorId a_actor, const TrackedActor &a_record) noexcept;
[[nodiscard]] BudgetActors BudgetActorsOf(const World &a_world);
[[nodiscard]] std::vector<WaitingStepUp> WaitingStepUpsOf(const World &a_world);
[[nodiscard]] std::vector<EvictedActor> EvictedActorsOf(const World &a_world);
[[nodiscard]] std::uint64_t LargestReturnBytes(const World &a_world) noexcept;
[[nodiscard]] std::optional<float>
FarthestTrackedDistance(const World &a_world) noexcept;
}
