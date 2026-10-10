// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Commands.h"
#include "orchestration/Facts.h"
#include "orchestration/RenderPhase.h"
#include "orchestration/World.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <string_view>

namespace BetterEnchantmentEffects::Orchestration {
[[nodiscard]] ActorSight ActorSightOf(const Sighting &a_sight) noexcept;
[[nodiscard]] TickActor TickActorFor(ActorId a_actor,
                                     const RenderPhase &a_phase,
                                     const Sighting &a_sight,
                                     const TickSettings &a_settings) noexcept;
[[nodiscard]] std::string_view EventName(const RenderEvent &a_event) noexcept;
}
