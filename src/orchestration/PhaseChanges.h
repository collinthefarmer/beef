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
  bool ignored = false;
};

struct EventContext {
  std::uint64_t frame = 0;
  std::optional<Level> wanted;
};

struct SeenThisTick {
  bool inView = false;
  bool small = false;
  std::optional<Level> wanted;
  Millis now = 0;
  Millis outOfViewReleaseDelay = 0;
  std::uint64_t releasableBytes = 0;
};

[[nodiscard]] std::optional<PhaseChange>
PhaseFor(ActorId a_actor, const RenderEvent &a_event, std::uint64_t a_frame);
[[nodiscard]] PhaseChange OnEvent(ActorId a_actor, RenderPhase a_phase,
                                  const RenderEvent &a_event,
                                  const EventContext &a_context);
[[nodiscard]] PhaseChange PlanBuilt(ActorId a_actor, RenderPhase a_phase,
                                    PlanJobId a_job, InstallBound a_bound);
[[nodiscard]] PhaseChange FailPlan(ActorId a_actor, RenderPhase a_phase,
                                   const PlanJobFailed &a_failed);
[[nodiscard]] PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase,
                                    Level a_level);
[[nodiscard]] PhaseChange ReleaseForBudget(ActorId a_actor, RenderPhase a_phase,
                                           std::uint64_t a_bytes);
[[nodiscard]] PhaseChange SeeActor(ActorId a_actor, RenderPhase a_phase,
                                   const SeenThisTick &a_seen);
[[nodiscard]] PhaseChange EndFirstDraw(RenderPhase a_phase);
}
