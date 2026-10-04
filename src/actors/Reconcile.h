// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"

#include <span>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
[[nodiscard]] std::vector<ActorCommand> Reconcile(const ActorPlan &a_installed,
                                                  const ActorPlan &a_plan);

[[nodiscard]] ActorPlan AfterCommand(ActorPlan a_installed,
                                     const ActorCommand &a_command);

[[nodiscard]] ActorPlan AfterCommands(ActorPlan a_installed,
                                      std::span<const ActorCommand> a_commands);
}
