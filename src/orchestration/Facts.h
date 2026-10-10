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

struct PlanJobBuilt {
  PlanJobId job{};
};

struct PlanJobFailed {
  PlanJobId job{};
  std::string problem;
};

using PlanJobDone = std::variant<PlanJobBuilt, PlanJobFailed>;

struct PlanAwaited {
  PlanJobId job{};
};

struct Installed {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::size_t units = 0;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::uint64_t chargedBytes = 0;
};

struct InstalledWithoutRender {};

struct UnpublishedDrawn {
  std::size_t drawn = 0;
  bool drewThisTick = false;
};

struct ResizeStarted {
  std::size_t units = 0;
};

struct ResizeNotStarted {
  std::string reason;
};

struct ResizeSwapped {};

struct ResizeNotSwapped {};

using RenderEvent =
    std::variant<PlanAwaited, Installed, InstalledWithoutRender,
                 UnpublishedDrawn, ResizeStarted, ResizeNotStarted,
                 ResizeSwapped, ResizeNotSwapped>;

struct ActorEvent {
  ActorId actor{};
  RenderEvent event;
};

struct InstallFailed {
  ActorId actor{};
  std::string reason;
};

struct Retired {
  ActorId actor{};
};

using CommandResult = std::variant<ActorEvent, InstallFailed, Retired>;

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
