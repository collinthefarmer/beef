// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/DrawSpread.h"
#include "test_support.h"

#include <cstddef>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Orchestration;
using test::Check;
using test::Equal;

namespace {
constexpr ActorId kSteady = ActorOf(0x14);
constexpr ActorId kRetiring = ActorOf(0x20);
constexpr ActorId kUnseen = ActorOf(0x30);
constexpr ActorId kResizing = ActorOf(0x40);

TickActor Drawing(ActorId a_actor) {
  return {.actor = a_actor, .drawStacks = true, .finishApplications = true};
}

void SteadyDrawsSpread() {
  const SpreadCommands split = SpreadTickActors(
      {Drawing(kSteady), Retire{kRetiring}, Drawing(kRetiring),
       TickActor{.actor = kUnseen, .finishApplications = true},
       Drawing(kResizing), DrawUnpublished{{kResizing}, 4000}});
  Equal(split.spread.size(), std::size_t{1},
        "only the actor with no other command spreads");
  Check(!split.spread.empty() && split.spread.front().actor == kSteady,
        "the steady drawing actor spreads");
  Equal(split.now.size(), std::size_t{5},
        "every other command runs in the tick");
  Check(split.now.size() == 5 && Is<Retire>(split.now[0]) &&
            Is<TickActor>(split.now[1]) && Is<TickActor>(split.now[2]) &&
            Is<TickActor>(split.now[3]) && Is<DrawUnpublished>(split.now[4]),
        "the tick keeps its commands in their order");
}

void NamedActors() {
  Check(ActorsNamedBy(DrawUnpublished{{kSteady, kResizing}, 0}) ==
            std::vector<ActorId>{kSteady, kResizing},
        "an unpublished draw names each actor it draws");
  Check(ActorsNamedBy(CancelPlanJob{}).empty(),
        "a plan job cancel names no actor");
  Check(ActorsNamedBy(Retire{kRetiring}) == std::vector<ActorId>{kRetiring},
        "a retire names its actor");
}

void DueDraws() {
  Equal(DrawsDue(9, 4), std::size_t{3},
        "nine draws over four frames take three now");
  Equal(DrawsDue(6, 3), std::size_t{2}, "the rest divide over the frames left");
  Equal(DrawsDue(4, 1), std::size_t{4}, "the last frame takes every draw");
  Equal(DrawsDue(4, 0), std::size_t{4},
        "an unknown frame count takes every draw");
  Equal(DrawsDue(0, 4), std::size_t{0}, "no draws waiting take none");
  std::size_t waiting = 10;
  for (std::uint64_t left = 4; left > 0; --left) {
    waiting -= DrawsDue(waiting, left);
  }
  Equal(waiting, std::size_t{0}, "the frames of one tick draw every actor");
}
}

int main() {
  SteadyDrawsSpread();
  NamedActors();
  DueDraws();
  return test::Finish("draw spread");
}
