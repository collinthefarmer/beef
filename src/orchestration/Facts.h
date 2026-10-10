// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
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

struct ObservedActor {
  ActorId actor{};
  Actors::ActorFacts facts;
};

struct PlanJobDone {
  PlanJob job{};
  bool built = false;
  std::string problem;
};

struct InstallAwaited {
  ActorId actor{};
  PlanJob job{};
};

struct InstallRendered {
  ActorId actor{};
  ResolutionLevel level = ResolutionLevel::kFull;
  std::size_t firstDrawSlots = 0;
  PerLevel<std::optional<LevelCost>> costs;
  std::uint64_t chargedBytes = 0;
};

struct InstallBare {
  ActorId actor{};
};

struct InstallFailed {
  ActorId actor{};
  std::string reason;
};

struct ActorRetired {
  ActorId actor{};
};

struct ResizeBuilt {
  ActorId actor{};
  std::size_t stacks = 0;
};

struct ResizeRefused {
  ActorId actor{};
  std::string reason;
};

struct ResizeSwapped {
  ActorId actor{};
};

struct ResizeMismatched {
  ActorId actor{};
};

struct HiddenDrawn {
  ActorId actor{};
  std::size_t drawn = 0;
  bool drewThisTick = false;
};

using CommandResult =
    std::variant<InstallAwaited, InstallRendered, InstallBare, InstallFailed,
                 ActorRetired, ResizeBuilt, ResizeRefused, ResizeSwapped,
                 ResizeMismatched, HiddenDrawn>;

struct TickFacts {
  Millis now = 0;
  std::uint64_t frame = 0;
  TickSettings settings;
  std::vector<ActorSighting> sightings;
  std::vector<CommandResult> results;
  std::vector<PlanJobDone> jobs;
  std::vector<ActorId> stale;
  std::vector<ObservedActor> observed;
  std::optional<BudgetSample> memory;
  FrameSyncs frameSyncs;
};
}
