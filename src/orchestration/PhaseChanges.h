// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Commands.h"
#include "orchestration/Facts.h"
#include "orchestration/RenderPhase.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct PhaseChange {
  RenderPhase phase;
  std::vector<Command> commands;
};

struct SeenActor {
  bool inView = true;
  bool small = false;
  std::optional<ResolutionLevel> wanted;
  Millis now = 0;
  Millis outOfViewReleaseDelay = 0;
  std::uint64_t releasableBytes = 0;
};

[[nodiscard]] PhaseChange OnEvent(ActorId a_actor, RenderPhase a_phase,
                                  const RenderEvent &a_event,
                                  std::uint64_t a_frame);
[[nodiscard]] PhaseChange FailPlan(ActorId a_actor, RenderPhase a_phase,
                                   PlanJobId a_job);
[[nodiscard]] PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase,
                                    ResolutionLevel a_level);
[[nodiscard]] PhaseChange ReleaseForBudget(ActorId a_actor, RenderPhase a_phase,
                                           std::uint64_t a_bytes);
[[nodiscard]] PhaseChange SeeActor(ActorId a_actor, RenderPhase a_phase,
                                   const SeenActor &a_seen);
[[nodiscard]] PhaseChange EndFirstDraw(RenderPhase a_phase);
}
