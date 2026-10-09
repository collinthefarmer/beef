// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResourcePool.h"
#include "test_support.h"

#include <limits>
#include <memory>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Target {
  int size = 0;
  std::shared_ptr<int> lease;
};
std::unique_ptr<Target> Make(int a_size) {
  return std::make_unique<Target>(a_size, std::make_shared<int>(a_size));
}

void FullPoolLetsTheOldestGo() {
  ResourcePool<Target> pool{2, 100};
  auto first = Make(10);
  const std::weak_ptr<int> oldest = first->lease;
  Check(pool.Retain(std::move(first), 40).empty() &&
            pool.Retain(Make(20), 60).empty(),
        "idle targets fit exact count and byte budgets");
  ResourcePool<Target>::Released released = pool.Retain(Make(30), 1);
  Check(released.size() == 1 && released.front()->size == 10 &&
            pool.Size() == 2 && pool.Bytes() == 61,
        "a full pool lets its oldest target go for the newest");
  released.clear();
  Check(oldest.expired(), "the caller destroys what the pool let go");
  Check(pool.Retain(Make(40), 100).size() == 2 && pool.Bytes() == 100,
        "byte pressure lets every older target go when the newest needs it");
  Check(
      pool.Retain(Make(50), std::numeric_limits<std::uint64_t>::max()).size() ==
              1 &&
          pool.Bytes() == 100,
      "a target over the byte budget is let go at once without overflow");
}

void TakeTransfersOwnership() {
  ResourcePool<Target> pool{2, 100};
  auto first = Make(10);
  const std::weak_ptr<int> lease = first->lease;
  Check(pool.Retain(std::move(first), 40).empty(), "an idle target fits");
  auto live =
      pool.Take([](const Target &a_target) { return a_target.size == 10; });
  Check(live && pool.Size() == 0 && pool.Bytes() == 0 && !lease.expired(),
        "matching acquisition transfers ownership and releases accounting");
  Check(!pool.Take([](const Target &a_target) { return a_target.size == 99; }),
        "unmatched acquisition takes nothing");
  Check(pool.Retain(std::move(live), 40).empty(),
        "a released consumer can reenter the pool");
  pool.Clear();
  Check(lease.expired(), "clearing releases the idle targets");
}

void IdleTargetsExpire() {
  ResourcePool<Target> pool{4, 100};
  Check(pool.Retain(Make(10), 10).empty(), "an idle target fits");
  Check(pool.Expire(1000, 5000).empty(),
        "a sweep starts the idle time of a new target");
  Check(pool.Retain(Make(20), 10).empty(), "a second target fits");
  Check(pool.Expire(5999, 5000).empty() && pool.Size() == 2,
        "nothing expires before its idle time");
  ResourcePool<Target>::Released expired = pool.Expire(6000, 5000);
  Check(expired.size() == 1 && expired.front()->size == 10 &&
            pool.Size() == 1 && pool.Bytes() == 10,
        "a target idle for the limit expires");
  Check(pool.Expire(10998, 5000).empty() &&
            pool.Expire(10999, 5000).size() == 1 && pool.Size() == 0,
        "each target counts from the sweep that first saw it idle");
  Check(pool.Retain(Make(30), 10).empty() && pool.Expire(20000, 5000).empty() &&
            pool.Expire(500, 5000).empty() && pool.Size() == 1,
        "a clock behind the idle start expires nothing");
}

void BoundsHoldForTinyAndNullTargets() {
  ResourcePool<Target> counts{1, 100};
  Check(counts.Retain(Make(1), 1).empty() &&
            counts.Retain(Make(2), 1).size() == 1 && counts.Size() == 1,
        "the count budget also bounds tiny targets");
  Check(counts.Retain(nullptr, 0).empty() && counts.Size() == 1,
        "null resources do not consume pool entries");
  ResourcePool<Target> none{0, 100};
  Check(none.Retain(Make(3), 1).size() == 1 && none.Size() == 0,
        "a pool without room lets every target go");
}
}

int main() {
  FullPoolLetsTheOldestGo();
  TakeTransfersOwnership();
  IdleTargetsExpire();
  BoundsHoldForTinyAndNullTargets();
  return test::Finish("planners_resourcepool");
}
