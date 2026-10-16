// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Commands.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct SpreadCommands {
  std::vector<Command> now;
  std::vector<TickActor> spread;
};

[[nodiscard]] std::vector<ActorId> ActorsNamedBy(const Command &a_command);
[[nodiscard]] SpreadCommands SpreadTickActors(std::vector<Command> a_commands);
[[nodiscard]] std::size_t DrawsDue(std::size_t a_waiting,
                                   std::uint64_t a_framesLeft) noexcept;
}
