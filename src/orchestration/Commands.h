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
enum class ActorDraw : std::uint8_t { kSkip, kVisible, kHidden };

enum class ReleaseReason : std::uint8_t { kHidden, kBudget };

struct UpdateActor {
  ActorId actor{};
  ActorDraw draw = ActorDraw::kSkip;
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
  ReleaseReason reason = ReleaseReason::kHidden;
  std::uint64_t bytes = 0;
};

struct DrawHidden {
  std::vector<ActorId> order;
};

struct RebuildRender {
  ActorId actor{};
  InstallBound bound;
};

struct FailRender {
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

struct MarkStale {
  ActorId actor{};
};

using Command =
    std::variant<UpdateActor, StartResize, SwapResize, AbandonResize,
                 ReleaseIntermediates, DrawHidden, RebuildRender, FailRender,
                 Install, Retire, Forget, MarkStale>;

struct TraceRecord {
  Trace::Event event = Trace::Event::kMetrics;
  std::vector<Trace::Field> fields;
};
}
