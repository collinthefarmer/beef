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
}

int main() {
  ResourcePool<Target> pool{2, 100};
  auto first = Make(10);
  const std::weak_ptr<int> released = first->lease;
  Check(pool.Retain(std::move(first), 40) && pool.Retain(Make(20), 60),
        "idle targets fit exact count and byte budgets");
  auto overflow = Make(30);
  const std::weak_ptr<int> refused = overflow->lease;
  const bool retained = pool.Retain(std::move(overflow), 1);
  Check(!retained && refused.expired(),
        "over-budget target is destroyed instead of retaining peak allocation");
  auto live =
      pool.Take([](const Target &a_target) { return a_target.size == 10; });
  Check(
      live && pool.Size() == 1 && pool.Bytes() == 60 && !released.expired(),
      "matching acquisition transfers ownership and releases idle accounting");
  Check(!pool.Retain(Make(30), 41) && pool.Bytes() == 60,
        "byte pressure refuses targets even with count headroom");
  Check(!pool.Retain(Make(30), std::numeric_limits<std::uint64_t>::max()),
        "oversized accounting cannot overflow the budget check");
  Check(!pool.Take([](const Target &a_target) {
    return a_target.size == 99;
  }) && pool.Bytes() == 60,
        "unmatched acquisition leaves idle resources intact");
  pool.Clear();
  Check(pool.Size() == 0 && pool.Bytes() == 0 && !released.expired(),
        "pool clearing leaves outstanding consumers valid");
  Check(pool.Retain(std::move(live), 40),
        "a released consumer can reenter the pool");
  pool.Clear();
  Check(released.expired(), "final idle clear releases the resource lease");
  ResourcePool<Target> counts{1, 100};
  Check(counts.Retain(Make(1), 1) && !counts.Retain(Make(2), 1),
        "count budget also bounds tiny targets");
  Check(!counts.Retain(nullptr, 0),
        "null resources do not consume pool entries");
  return test::Finish("planners_resourcepool");
}
