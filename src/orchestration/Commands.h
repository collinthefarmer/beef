// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "actors/Planning.h"
#include "actors/SyncLog.h"
#include "diagnostics/Trace.h"
#include "orchestration/World.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
enum class ReleaseReason : std::uint8_t { kOutOfView, kBudget };

struct TickActor {
  ActorId actor{};
  bool drawStacks = false;
  bool concealSlots = false;
  bool hideShells = false;
  bool hideLights = false;
  bool finishApplications = false;
};

struct StartResize {
  ActorId actor{};
  ResolutionLevel to = ResolutionLevel::kFull;
};

struct SwapResize {
  ActorId actor{};
};

struct AbandonResize {
  ActorId actor{};
};

struct ReleaseIntermediates {
  ActorId actor{};
  ReleaseReason reason = ReleaseReason::kOutOfView;
  std::uint64_t bytes = 0;
};

struct SetAppliedLevel {
  ActorId actor{};
  ResolutionLevel level = ResolutionLevel::kFull;
};

struct DrawUnpublished {
  std::vector<ActorId> order;
  std::uint64_t budgetUs = 0;
};

struct BuildAwaitedRender {
  ActorId actor{};
  InstallBound bound;
};

struct FailAwaitedRender {
  ActorId actor{};
  std::string problem;
};

struct Install {
  ActorId actor{};
  Actors::ActorFacts facts;
  Actors::PlannedActor planned;
  Actors::SyncKind kind = Actors::SyncKind::kInstalled;
  InstallBound bound;
  std::uint64_t replacedBytes = 0;
};

struct Retire {
  ActorId actor{};
};

struct Forget {
  ActorId actor{};
};

struct Observe {
  ActorId actor{};
};

using Command =
    std::variant<TickActor, StartResize, SwapResize, AbandonResize,
                 ReleaseIntermediates, SetAppliedLevel, DrawUnpublished,
                 BuildAwaitedRender, FailAwaitedRender, Install, Retire, Forget,
                 Observe>;

struct TraceRecord {
  Trace::Event event = Trace::Event::kMetrics;
  std::vector<Trace::Field> fields;
};
}
