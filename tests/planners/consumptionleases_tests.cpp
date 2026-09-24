// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ConsumptionLeases.h"
#include "test_support.h"
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  ConsumptionLeases<int> draws{2};
  auto resource = std::make_shared<int>(3);
  std::weak_ptr<int> lifetime = resource;
  auto *first = draws.Retain(resource);
  auto *second = draws.Retain(resource);
  Check(first && second && !draws.Retain(resource),
        "pressure refuses new work without evicting pending consumers");
  resource.reset();
  draws.Collect();
  Check(!lifetime.expired(),
        "producer retirement does not release pending draws");
  first->Consumed();
  draws.Collect();
  Check(!lifetime.expired(),
        "one completed draw cannot release another draw's lease");
  second->Consumed();
  draws.Collect();
  Check(lifetime.expired(), "last acknowledged draw releases the resource");
  Check(draws.Retain(std::make_shared<int>(5)),
        "completed work returns queue capacity");
  return test::Finish("consumption leases");
}
