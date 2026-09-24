// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ResourceCache.h"
#include "test_support.h"

#include <memory>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
struct Made {
  int id = 0;
};

int g_made = 0;

std::shared_ptr<Made> Make(int a_id) {
  ++g_made;
  return std::make_shared<Made>(a_id);
}
}

int main() {
  ResourceCache<Made> cache;
  g_made = 0;

  const SharedResource<Made> first = cache.Adopt("a", [] { return Make(1); });
  Check(first.value && first.value->id == 1 && !first.adopted,
        "the first Adopt makes and does not report an adoption");
  const SharedResource<Made> second = cache.Adopt("a", [] { return Make(2); });
  Check(second.value == first.value && second.adopted,
        "a second Adopt of a live key reuses the same value and reports it");
  Equal(g_made, 1, "the supplier runs only on the miss");
  Equal(cache.LiveCount(), std::size_t{1}, "one entry is live while held");

  g_made = 0;
  std::shared_ptr<Made> held = cache.Adopt("b", [] { return Make(3); }).value;
  Equal(cache.LiveCount(), std::size_t{2}, "a second key adds a live entry");
  held.reset();
  Equal(cache.LiveCount(), std::size_t{1},
        "dropping the last holder lets the weak entry expire");
  const SharedResource<Made> again = cache.Adopt("b", [] { return Make(4); });
  Check(again.value && again.value->id == 4 && !again.adopted,
        "adopting an expired key makes a fresh value");
  Equal(g_made, 2,
        "the supplier ran once for the first make and once for the remake");

  const SharedResource<Made> refused =
      cache.Adopt("c", []() -> std::shared_ptr<Made> { return nullptr; });
  Check(!refused.value && !refused.adopted,
        "a supplier that returns null yields no value and caches nothing");
  Equal(cache.LiveCount(), std::size_t{2},
        "a refused make does not add a live entry");

  Check(cache.Size() == 2, "failed creation leaves no retained key");
  for (int i = 0; i < 1000; ++i) {
    auto transient = cache.Adopt(std::to_string(i), [] { return Make(5); });
  }
  Check(cache.Size() <= 3,
        "authoring churn does not retain expired serialized keys");
  cache.Sweep();
  Check(cache.Size() == 2 && first.value == second.value,
        "idle sweeping removes expired keys without disturbing active sharing");
  cache.Clear();
  Equal(cache.LiveCount(), std::size_t{0}, "Clear drops every entry");

  return test::Finish("planners_resourcecache");
}
