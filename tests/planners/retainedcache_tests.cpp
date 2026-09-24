// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RetainedCache.h"
#include "test_support.h"

#include <array>
#include <memory>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  RetainedCache<int, std::shared_ptr<int>> cache;
  cache.Get(1, 0) = std::make_shared<int>(42);
  const auto held = *cache.Find(1);
  const std::weak_ptr<int> weak = held;
  const std::array keep{1};
  for (int i = 2; i <= 8; ++i) {
    cache.Get(i, static_cast<std::uint32_t>(i)) = std::make_shared<int>(i);
  }
  cache.Sweep(10, 30, 2, keep);
  Check(cache.Size() == 3 && cache.Find(1) && cache.Find(7) && cache.Find(8) &&
            !cache.Find(2),
        "pressure keeps active entries and the newest unused entries");
  cache.Sweep(100, 30, 2, keep);
  Check(cache.Size() == 1 && cache.Find(1),
        "active entries survive expiry and unused values are removed");
  cache.Sweep(129, 30, 2, {});
  Check(cache.Find(1), "newly inactive values retain a grace period");
  cache.Sweep(130, 30, 2, {});
  Check(cache.Size() == 0 && !weak.expired() && *held == 42,
        "empty-world maintenance expires cache ownership without invalidating "
        "consumers");
  cache.Get(2, 0xfffffff0u) = std::make_shared<int>(2);
  cache.Sweep(5, 30, 2, {});
  Check(cache.Find(2), "age calculation survives clock wrap");
  cache.Sweep(14, 30, 2, {});
  Check(!cache.Find(2), "wrapped timestamps expire at the exact boundary");
  cache.Get(1, 0) = std::make_shared<int>(1);
  cache.Get(2, 1) = std::make_shared<int>(2);
  cache.Get(1, 2);
  cache.Sweep(3, 30, 1, {});
  Check(cache.Find(1) && !cache.Find(2), "access refreshes pressure ordering");
  cache.Sweep(4, 30, 0, keep);
  Check(cache.Find(1), "active values survive a zero unused budget");
  cache.Clear();
  Check(cache.Size() == 0, "session clear drops retained cache entries");
  RetainedCache<int, std::string> failures;
  failures.Get(1, 0) = "temporary readback failure";
  failures.Sweep(30, 30, 64, {});
  Check(!failures.Find(1), "unused failure records expire too");
  return test::Finish("planners_retainedcache");
}
