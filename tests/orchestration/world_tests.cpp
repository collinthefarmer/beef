// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Tick.h"
#include "test_support.h"

#include <type_traits>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using test::Check;

static_assert(kAlternativesNothrowMovable<RenderPhase>);
static_assert(kAlternativesNothrowMovable<RenderEvent>);
static_assert(kAlternativesNothrowMovable<CommandResult>);
static_assert(kAlternativesNothrowMovable<Command>);
static_assert(std::is_nothrow_move_constructible_v<World>);
static_assert(kAlternativesNothrowMovable<PlanJobDone>);
static_assert(kAlternativesNothrowMovable<Intermediates>);
static_assert(std::is_nothrow_move_constructible_v<TickOutput>);

int main() {
  const World world;
  Check(world.actors.empty() && world.waitingInstalls.empty() && !world.radius,
        "a new world tracks no actor");
  const InstalledRender render;
  Check(Is<Empty>(render.phase) && !render.renderedFrame,
        "a render record made without an event claims no render");
  Check(FormOf(ActorOf(0x14)) == 0x14u,
        "an actor id converts to a form id and back");
  const TickFacts facts;
  Check(facts.sightings.empty() && facts.results.empty() && !facts.memory,
        "empty facts carry no sighting, result or memory sample");
  const TickOutput empty = Tick(World{}, TickFacts{});
  Check(empty.world.actors.empty() && empty.commands.empty() &&
            empty.trace.empty() && empty.logs.empty(),
        "a tick over an empty world decides nothing");
  return test::Finish("orchestration_world");
}
