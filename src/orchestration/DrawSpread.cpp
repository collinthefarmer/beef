// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/DrawSpread.h"

#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
ActorSet ActorsWithOtherCommands(const std::vector<Command> &a_commands);
bool IsSpreadable(const Command &a_command, const ActorSet &a_busy);
}

std::vector<ActorId> ActorsNamedBy(const Command &a_command) {
  return Match(
      a_command, [](const DrawUnpublished &a_draw) { return a_draw.order; },
      [](const CancelPlanJob &) { return std::vector<ActorId>{}; },
      [](const auto &a_other) { return std::vector<ActorId>{a_other.actor}; });
}

SpreadCommands SpreadTickActors(std::vector<Command> a_commands) {
  const ActorSet busy = ActorsWithOtherCommands(a_commands);
  SpreadCommands split;
  for (Command &command : a_commands) {
    if (IsSpreadable(command, busy)) {
      split.spread.push_back(*Get<TickActor>(command));
      continue;
    }
    split.now.push_back(std::move(command));
  }
  return split;
}

std::size_t DrawsDue(std::size_t a_waiting,
                     std::uint64_t a_framesLeft) noexcept {
  if (a_framesLeft <= 1) {
    return a_waiting;
  }
  return static_cast<std::size_t>((a_waiting + a_framesLeft - 1) /
                                  a_framesLeft);
}

namespace {
ActorSet ActorsWithOtherCommands(const std::vector<Command> &a_commands) {
  ActorSet busy;
  for (const Command &command : a_commands) {
    if (Is<TickActor>(command)) {
      continue;
    }
    for (const ActorId actor : ActorsNamedBy(command)) {
      busy.insert(actor);
    }
  }
  return busy;
}

bool IsSpreadable(const Command &a_command, const ActorSet &a_busy) {
  const auto *tick = Get<TickActor>(a_command);
  return tick && tick->drawStacks && !a_busy.contains(tick->actor);
}
}
}
