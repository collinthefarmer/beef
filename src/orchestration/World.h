// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/SyncLog.h"
#include "planners/ActorPhase.h"
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

enum class PlanJob : std::uint64_t {};

struct FirstDrawCount {
  std::uint64_t beganFrame = 0;
  std::uint64_t drawTicks = 0;
  std::size_t stacks = 0;
  std::size_t drawn = 0;
  [[nodiscard]] bool operator==(const FirstDrawCount &) const = default;
};

struct ResizeCount {
  std::size_t stacks = 0;
  std::size_t drawn = 0;
  [[nodiscard]] bool operator==(const ResizeCount &) const = default;
};

using RenderPhase = ActorPhase<PlanJob, FirstDrawCount, ResizeCount>;
using RenderEvent = PhaseEvent<PlanJob, FirstDrawCount, ResizeCount>;

struct Sighting {
  bool loaded = false;
  bool inView = false;
  std::optional<float> sizingFraction;
  std::optional<float> distance;
  [[nodiscard]] bool operator==(const Sighting &) const = default;
};

struct LiveRender {
  RenderPhase phase;
  PerLevel<std::optional<LevelCost>> costs;
  bool attached = false;
  std::optional<std::uint64_t> renderedFrame;
};

struct ActorRecord {
  ActorRole role = ActorRole::kOther;
  Sighting sight;
  std::optional<Actors::ActorState> sync;
  std::optional<ResolutionLevel> reinstallLevel;
  std::optional<std::uint64_t> returnBytes;
  std::optional<LiveRender> render;
};

struct Timers {
  Millis lastTick = 0;
  Millis lastWaitingCheck = 0;
  Millis lastRecheck = 0;
  Millis lastBudget = 0;
  [[nodiscard]] bool operator==(const Timers &) const = default;
};

struct World {
  std::map<ActorId, ActorRecord> actors;
  BudgetState budget;
  std::optional<BudgetRadius> radius;
  std::vector<WaitingInstall> waiting;
  std::vector<Actors::SyncRecord> syncLog;
  Timers timers;
  std::uint64_t frame = 0;
};
}
