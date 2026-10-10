// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/Lifecycle.h"
#include "actors/Planning.h"
#include "orchestration/World.h"
#include "planners/InstallPacing.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct TickSettings {
  float evictDistance = 0.0f;
  bool sizeByScreen = true;
  bool publishEffects = true;
  bool verboseLogging = false;
  std::uint32_t unpublishedDrawBudgetMs = 4;
  Millis outOfViewReleaseDelayMs = 5000;
  Millis screenStepDownHoldMs = 2000;
  [[nodiscard]] bool operator==(const TickSettings &) const = default;
};

struct StaleActor {
  ActorId actor{};
  Actors::ActorFacts facts;
  Actors::PlannedActor planned;
  Actors::Reinstall reinstall = Actors::Reinstall::kIfChanged;
};

struct ClaimedGeometry {
  ActorId actor{};
  Actors::Claim claim;
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
  std::size_t total = 0;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::uint64_t chargedBytes = 0;
};

struct InstalledWithoutRender {};

struct UnpublishedDrawn {
  std::size_t drawn = 0;
  bool drewThisTick = false;
};

struct ResizeStarted {
  std::size_t total = 0;
};

struct ResizeNotStarted {
  std::string problem;
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
  std::string problem;
};

struct Retired {
  ActorId actor{};
};

using CommandResult = std::variant<ActorEvent, InstallFailed, Retired>;

struct TickFacts {
  Millis now = 0;
  std::uint64_t frame = 0;
  TickSettings settings;
  std::map<ActorId, Sighting> sightings;
  std::vector<CommandResult> results;
  std::vector<PlanJobDone> jobs;
  std::vector<StaleActor> stale;
  std::vector<ClaimedGeometry> claims;
  std::optional<BudgetSample> memory;
  FrameSyncs frameSyncs;
  std::vector<ActorId> forgets;
};
}
