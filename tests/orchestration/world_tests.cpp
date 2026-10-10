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
static_assert(std::is_nothrow_move_constructible_v<StepOutput>);

int main() {
  const World world;
  Check(world.actors.empty() && world.waiting.empty() && !world.radius &&
            world.frame == 0,
        "a new world tracks no actor");
  const LiveRender render;
  Check(Is<Live>(render.phase) && !render.renderedFrame && !render.attached,
        "a new render is live, unattached and never rendered");
  const TickFacts facts;
  Check(facts.sightings.empty() && facts.results.empty() && !facts.memory,
        "empty facts carry no sighting, result or memory sample");
  return test::Finish("orchestration_world");
}
