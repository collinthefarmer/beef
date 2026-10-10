// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/SyncLog.h"
#include "orchestration/RenderPhase.h"
#include "planners/ActorRole.h"
#include "planners/BudgetRadius.h"
#include "planners/Hold.h"
#include "planners/InstallPacing.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
using ActorId = Actors::ActorId;

struct Sighting {
  bool loaded3D = false;
  bool inView = false;
  std::optional<float> sizingFraction;
  std::optional<float> distance;
  [[nodiscard]] bool operator==(const Sighting &) const = default;
};

struct InstalledRender {
  RenderPhase phase;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::optional<std::uint64_t> renderedFrame;
};

struct ActorRecord {
  ActorRole role = ActorRole::kOther;
  Sighting sight;
  std::optional<Actors::ActorState> syncState;
  std::optional<ResolutionLevel> reinstallLevel;
  std::optional<std::uint64_t> returnBytes;
  std::optional<InstalledRender> render;
};

struct Timers {
  Millis lastWaitingCheck = 0;
  Millis lastRecheck = 0;
  [[nodiscard]] bool operator==(const Timers &) const = default;
};

struct World {
  std::map<ActorId, ActorRecord> actors;
  BudgetState budget;
  std::optional<BudgetRadius> radius;
  std::vector<WaitingInstall> waitingInstalls;
  std::vector<Actors::SyncRecord> syncLog;
  Timers timers;
};
}
