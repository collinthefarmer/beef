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
  Check(MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs - 1)),
        "a tick inside the budget draws another stack");
  Check(!MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs)),
        "a tick that spent the budget draws no further stack");
  const DrawPace twice = Drew(Drew({}, 1000), 1500);
  Check(twice.stacks == 2 && twice.spentUs == 2500,
        "each drawn stack adds its time and itself");
}

void BudgetIsAnArgument() {
  const DrawPace spent = Drew({}, 2000);
  Check(MayDrawStack({}, 1000), "a tick draws its first stack at any budget");
  Check(!MayDrawStack(spent, 2000),
        "a tick that spent the given budget draws no further stack");
  Check(MayDrawStack(spent, 2001),
        "a tick inside the given budget draws another stack");
  Check(MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs - 1)) ==
                MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs - 1),
                             kUnpublishedDrawBudgetUs) &&
            MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs)) ==
                MayDrawStack(Drew({}, kUnpublishedDrawBudgetUs),
                             kUnpublishedDrawBudgetUs),
        "the default budget is the fixed one");
}

void PlayerThenLargestOnScreen() {
  const std::vector<UnpublishedDrawCandidate> candidates{
      {1, ActorRole::kOther, 0.2f},
      {2, ActorRole::kOther, 0.8f},
      {3, ActorRole::kPlayer, 0.1f},
      {4, ActorRole::kOther, 0.5f}};
  Check(UnpublishedDrawOrder(candidates) ==
            std::vector<std::uint32_t>{3, 2, 4, 1},
        "the player draws first, then actors by screen fraction");
  Check(UnpublishedDrawOrder({}).empty(), "no candidates draw nothing");
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
  BudgetIsAnArgument();
  PlayerThenLargestOnScreen();
  AttachWaitsForEveryStack();
  return test::Finish("planners_drawpacing");
}
