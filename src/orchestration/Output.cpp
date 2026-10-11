// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Output.h"

#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
std::string ActorText(ActorId a_actor) {
  return std::to_string(FormOf(a_actor));
}

void TracePhase(TickOutput &a_output, ActorId a_actor, std::string_view a_from,
                std::string_view a_to, std::string_view a_event) {
  AddTrace(a_output, {{"action", "actor_phase"},
                      {"actor", ActorText(a_actor)},
                      {"from", std::string{a_from}},
                      {"to", std::string{a_to}},
                      {"event", std::string{a_event}}});
}

void TraceIgnored(TickOutput &a_output, ActorId a_actor,
                  std::string_view a_phase, std::string_view a_event) {
  AddTrace(a_output, {{"action", "actor_phase_ignored"},
                      {"actor", ActorText(a_actor)},
                      {"phase", std::string{a_phase}},
                      {"event", std::string{a_event}}});
}

void ApplyPhaseChange(TickOutput &a_output, ActorId a_actor,
                      InstalledRender &a_render, PhaseChange a_change,
                      std::string_view a_event) {
  const std::string_view before = PhaseName(a_render.phase);
  if (a_change.ignored) {
    TraceIgnored(a_output, a_actor, before, a_event);
  }
  a_render.phase = a_change.phase;
  if (const std::string_view after = PhaseName(a_render.phase);
      after != before) {
    TracePhase(a_output, a_actor, before, after, a_event);
  }
  Append(a_output.commands, std::move(a_change.commands));
}
}
