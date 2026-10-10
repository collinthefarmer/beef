// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/Planning.h"
#include "orchestration/World.h"
#include "planners/InstallPacing.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct TickSettings {
  float evictDistance = 0.0f;
  [[nodiscard]] bool operator==(const TickSettings &) const = default;
};

struct ActorSighting {
  ActorId actor{};
  ActorRole role = ActorRole::kOther;
  Sighting sight;
};

struct StaleActor {
  ActorId actor{};
  Actors::ActorFacts facts;
  Actors::PlannedActor planned;
};

struct PlanJobDone {
  PlanJobId job{};
  bool built = false;
  std::string problem;
};

struct AwaitsPlan {
  PlanJobId job{};
};

struct WithRender {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::size_t firstDrawSlots = 0;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::uint64_t chargedBytes = 0;
};

struct WithoutRender {};

using RenderOutcome = std::variant<AwaitsPlan, WithRender, WithoutRender>;

struct InstallDone {
  ActorId actor{};
  RenderOutcome outcome;
};

struct InstallFailed {
  ActorId actor{};
  std::string reason;
};

struct BuildAwaitedRenderDone {
  ActorId actor{};
  RenderOutcome outcome;
};

struct RetireDone {
  ActorId actor{};
};

struct StartResizeDone {
  ActorId actor{};
  std::size_t stacks = 0;
};

struct StartResizeFailed {
  ActorId actor{};
  std::string reason;
};

struct SwapResizeDone {
  ActorId actor{};
};

struct SwapResizeFailed {
  ActorId actor{};
};

struct DrawHiddenDone {
  ActorId actor{};
  std::size_t drawn = 0;
  bool drewThisTick = false;
};

using CommandResult =
    std::variant<InstallDone, InstallFailed, BuildAwaitedRenderDone, RetireDone,
                 StartResizeDone, StartResizeFailed, SwapResizeDone,
                 SwapResizeFailed, DrawHiddenDone>;

struct TickFacts {
  Millis now = 0;
  std::uint64_t frame = 0;
  TickSettings settings;
  std::vector<ActorSighting> sightings;
  std::vector<CommandResult> results;
  std::vector<PlanJobDone> jobs;
  std::vector<StaleActor> stale;
  std::optional<BudgetSample> memory;
  FrameSyncs frameSyncs;
};
}
