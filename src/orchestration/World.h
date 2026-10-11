// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
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
#include <set>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
using ActorId = Actors::ActorId;
using ActorSet = std::set<ActorId>;
using ActorList = std::vector<ActorId>;
using MaybeActor = std::optional<ActorId>;
using SyncState = std::optional<Actors::ActorState>;
using MaybeBytes = std::optional<std::uint64_t>;
using FormIds = std::vector<std::uint32_t>;

[[nodiscard]] constexpr std::uint32_t FormOf(ActorId a_actor) noexcept {
  return static_cast<std::uint32_t>(a_actor);
}

[[nodiscard]] constexpr ActorId ActorOf(std::uint32_t a_form) noexcept {
  return ActorId{a_form};
}

struct Sighting {
  ActorRole role = ActorRole::kOther;
  bool loaded3D = false;
  bool inView = false;
  std::optional<float> screenFraction;
  std::optional<float> distance;
  std::string name;
  [[nodiscard]] bool operator==(const Sighting &) const = default;
};

struct InstalledRender {
  RenderPhase phase;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::optional<std::uint64_t> renderedFrame;
};

struct ActorRecord {
  Sighting sight;
  SyncState syncState;
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

[[nodiscard]] inline ActorRecord *TrackedActor(World &a_world,
                                               ActorId a_actor) noexcept {
  return Lookup(a_world.actors, a_actor);
}

[[nodiscard]] inline const ActorRecord *TrackedActor(const World &a_world,
                                                     ActorId a_actor) noexcept {
  return Lookup(a_world.actors, a_actor);
}
}
