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
static_assert(kAlternativesNothrowMovable<RenderOutcome>);
static_assert(std::is_nothrow_move_constructible_v<TickOutput>);

int main() {
  const World world;
  Check(world.actors.empty() && world.waitingInstalls.empty() && !world.radius,
        "a new world tracks no actor");
  const InstalledRender render;
  Check(Is<Live>(render.phase) && !render.renderedFrame && !render.hasRender,
        "a new render is live, unattached and never rendered");
  const TickFacts facts;
  Check(facts.sightings.empty() && facts.results.empty() && !facts.memory,
        "empty facts carry no sighting, result or memory sample");
  return test::Finish("orchestration_world");
}
