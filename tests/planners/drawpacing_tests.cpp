// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/DrawPacing.h"
#include "test_support.h"

#include <optional>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void BudgetDrawsAtLeastOneStack() {
  Check(MayDrawStack({}), "a tick draws its first stack");
  Check(MayDrawStack(Drew({}, kHiddenDrawBudgetUs - 1)),
        "a tick inside the budget draws another stack");
  Check(!MayDrawStack(Drew({}, kHiddenDrawBudgetUs)),
        "a tick that spent the budget draws no further stack");
  const DrawPace twice = Drew(Drew({}, 1000), 1500);
  Check(twice.stacks == 2 && twice.spentUs == 2500,
        "each drawn stack adds its time and itself");
}

void PlayerThenLargestOnScreen() {
  const std::vector<HiddenDrawCandidate> candidates{{ActorRole::kOther, 0.2f},
                                                    {ActorRole::kOther, 0.8f},
                                                    {ActorRole::kPlayer, 0.1f},
                                                    {ActorRole::kOther, 0.5f}};
  Check(HiddenDrawOrder(candidates) == std::vector<std::size_t>{2, 1, 3, 0},
        "the player draws first, then actors by screen fraction");
  Check(HiddenDrawOrder({}).empty(), "no candidates draw nothing");
}

void AttachWaitsForEveryStack() {
  Check(!FirstDrawDone({3, 4, 3}), "a stack not yet drawn keeps the draw");
  Check(FirstDrawDone({3, 4, 4}) == std::optional{FirstDrawEnd::kDrawn},
        "every stack drawn attaches");
  Check(FirstDrawDone({0, 0, 0}) == std::optional{FirstDrawEnd::kDrawn},
        "an instance with no stacks attaches at once");
  Check(!FirstDrawDone({kFirstDrawAttachAfterTicks - 1, 4, 1}),
        "a draw inside the tick limit waits");
  Check(FirstDrawDone({kFirstDrawAttachAfterTicks, 4, 1}) ==
            std::optional{FirstDrawEnd::kTickLimit},
        "a draw that never finishes attaches at the tick limit");
}
}

int main() {
  BudgetDrawsAtLeastOneStack();
  PlayerThenLargestOnScreen();
  AttachWaitsForEveryStack();
  return test::Finish("planners_drawpacing");
}
