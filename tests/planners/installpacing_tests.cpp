// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InstallPacing.h"
#include "test_support.h"

#include <algorithm>
#include <optional>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::uint32_t kNow = 50000;

WaitingInstall Other(std::uint32_t a_actor, bool a_shown, float a_fraction,
                     std::uint32_t a_waited) {
  return {a_actor, ActorRole::kOther, a_shown, a_fraction, kNow - a_waited};
}

void BudgetAdmitsAtLeastOneInstall() {
  Check(CanInstallThisTick({}), "an empty frame starts an install");
  Check(CanInstallThisTick({kInstallBudgetUs * 4, 0}),
        "a frame of retires only still starts its first install");
  Check(!CanInstallThisTick({kInstallBudgetUs, 1}),
        "a frame that spent the budget starts no second install");
  Check(CanInstallThisTick({kInstallBudgetUs - 1, 3}),
        "a frame inside the budget starts another install");
  const FrameSyncs retired = Synced({}, 20000, false);
  Check(retired.installs == 0 && retired.spentUs == 20000,
        "a retire counts its time but not as an install");
  const FrameSyncs installed = Synced(retired, 15000, true);
  Check(installed.installs == 1 && installed.spentUs == 35000,
        "an install counts its time and itself");
}

void PlanSlotsLimitInstalls() {
  Check(!CanInstallThisTick({.planSlots = 0}),
        "no install starts without a free plan slot");
  Check(CanInstallThisTick({.installs = 1, .planSlots = 2}),
        "an install starts while a plan slot is free");
  Check(!CanInstallThisTick({.installs = 2, .planSlots = 2}),
        "installs stop once the free plan slots are used");
}

void OrderPutsThePlayerFirst() {
  const std::vector<WaitingInstall> waiting{
      Other(1, true, 0.9f, 10),
      {2, ActorRole::kPlayer, false, 0.0f, kNow - 5},
      Other(3, true, 0.3f, 400)};
  Check(NextInstall(waiting, {}, kNow) == std::optional<std::size_t>{1},
        "the player installs before every other actor");
}

void OrderPrefersLargerOnScreen() {
  const std::vector<WaitingInstall> waiting{
      Other(1, false, 0.0f, 900), Other(2, true, 0.2f, 300),
      Other(3, true, 0.7f, 10), Other(4, true, 0.5f, 500)};
  Check(NextInstall(waiting, {}, kNow) == std::optional<std::size_t>{2},
        "the actor in view largest on screen installs first");
  const std::vector<WaitingInstall> hidden{Other(1, false, 0.0f, 100),
                                           Other(2, false, 0.0f, 600)};
  Check(NextInstall(hidden, {}, kNow) == std::optional<std::size_t>{1},
        "among actors out of view the oldest installs first");
}

void InstallOrderSortsTheWholeQueue() {
  const std::vector<WaitingInstall> waiting{
      Other(1, false, 0.0f, 900), Other(2, true, 0.2f, 300),
      Other(3, true, 0.7f, 10), Other(4, true, 0.5f, 500)};
  Check(InstallOrder(waiting, kNow) == std::vector<std::uint32_t>{3, 4, 2, 1},
        "the queue sorts in install order, first install first");
  Check(InstallOrder({}, kNow).empty(), "an empty queue has no order");
}

void EveryArrivalOrderGivesOneInstallOrder() {
  std::vector<WaitingInstall> waiting{
      Other(1, false, 0.0f, 900),
      {2, ActorRole::kPlayer, false, 0.0f, kNow - 5},
      Other(3, true, 0.7f, 10),
      Other(4, true, 0.5f, 500),
      Other(5, false, 0.0f, kInstallWaitLimitMS + 1)};
  std::ranges::sort(waiting, {}, &WaitingInstall::actor);
  const std::vector<std::uint32_t> expected{5, 2, 3, 4, 1};
  bool same = true;
  bool firstAgrees = true;
  do {
    const std::vector<std::uint32_t> order = InstallOrder(waiting, kNow);
    same = same && order == expected;
    const std::optional<std::size_t> next = NextInstall(waiting, {}, kNow);
    firstAgrees = firstAgrees && next && waiting[*next].actor == order.front();
  } while (
      std::ranges::next_permutation(waiting, {}, &WaitingInstall::actor).found);
  Check(same, "every arrival order of the queue sorts the same way");
  Check(firstAgrees, "the next install is the first of the install order");
}

void OldEntriesAreNotStarved() {
  const std::vector<WaitingInstall> waiting{
      {1, ActorRole::kPlayer, true, 1.0f, kNow - 5},
      Other(2, false, 0.0f, kInstallWaitLimitMS),
      Other(3, true, 0.9f, 20)};
  Check(NextInstall(waiting, {}, kNow) == std::optional<std::size_t>{1},
        "an entry that waited the limit installs before the player");
  const std::vector<WaitingInstall> overdue{
      Other(1, false, 0.0f, kInstallWaitLimitMS + 10),
      Other(2, true, 0.9f, kInstallWaitLimitMS + 500)};
  Check(NextInstall(overdue, {}, kNow) == std::optional<std::size_t>{1},
        "among overdue entries the oldest installs first");
  const std::vector<WaitingInstall> wrapped{
      {7, ActorRole::kOther, false, 0.0f, 0xFFFFFF00u}};
  Check(*WaitedMS(wrapped, 7, 0x100u) == 0x200u,
        "the wait survives a wrap of the millisecond clock");
}

void SpentBudgetRunsNothing() {
  const std::vector<WaitingInstall> waiting{Other(1, true, 0.5f, 10)};
  Check(!NextInstall(waiting, {kInstallBudgetUs + 1, 1}, kNow),
        "no waiting install runs once the frame spent the budget");
  Check(!NextInstall({}, {}, kNow), "an empty queue runs nothing");
}

void EntriesKeepTheirFirstDeferral() {
  std::vector<WaitingInstall> waiting = Deferred({}, 5, 100);
  waiting = Deferred(std::move(waiting), 5, 900);
  Check(waiting.size() == 1, "a second deferral adds no second entry");
  Check(WaitedMS(waiting, 5, 1000) == std::optional<std::uint32_t>{900},
        "an entry waits from its first deferral");
  waiting = Removed(std::move(waiting), 5);
  Check(waiting.empty(), "an entry that ran leaves the queue");
  Check(!WaitedMS(waiting, 5, 1000), "a removed actor has no wait");
}
}

int main() {
  PlanSlotsLimitInstalls();
  InstallOrderSortsTheWholeQueue();
  EveryArrivalOrderGivesOneInstallOrder();
  BudgetAdmitsAtLeastOneInstall();
  OrderPutsThePlayerFirst();
  OrderPrefersLargerOnScreen();
  OldEntriesAreNotStarved();
  SpentBudgetRunsNothing();
  EntriesKeepTheirFirstDeferral();
  return test::Finish("planners_installpacing");
}
