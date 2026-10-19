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
  bool shown = false;
  std::optional<float> screenFraction;
  std::optional<float> distance;
  std::string name;
  ResolutionLevel finest = ResolutionLevel::kFull;
  [[nodiscard]] bool operator==(const Sighting &) const = default;
};

[[nodiscard]] constexpr ActorVisibility
VisibilityOf(const Sighting &a_sight) noexcept {
  return {a_sight.screenFraction, a_sight.shown, a_sight.role, a_sight.finest};
}

struct InstalledRender {
  RenderPhase phase;
  PerLevel<std::optional<LevelCost>> levelCosts;
  std::optional<std::uint64_t> renderedFrame;
};

struct TrackedActor {
  Sighting sight;
  SyncState syncState;
  std::optional<ResolutionLevel> reinstallLevel;
  std::optional<std::uint64_t> returnBytes;
  std::optional<InstalledRender> render;
};

struct Timers {
  Millis lastLoadingCheck = 0;
  Millis lastSlowCheck = 0;
  [[nodiscard]] bool operator==(const Timers &) const = default;
};

struct World {
  std::map<ActorId, TrackedActor> actors;
  BudgetState budget;
  std::optional<BudgetRadius> radius;
  std::vector<WaitingInstall> waitingInstalls;
  std::vector<Actors::SyncRecord> syncLog;
  Timers timers;
};

[[nodiscard]] inline TrackedActor *FindActor(World &a_world,
                                             ActorId a_actor) noexcept {
  return Lookup(a_world.actors, a_actor);
}

[[nodiscard]] inline const TrackedActor *FindActor(const World &a_world,
                                                   ActorId a_actor) noexcept {
  return Lookup(a_world.actors, a_actor);
}
}
