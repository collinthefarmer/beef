#include "planners/TargetPool.h"
#include "test_support.h"
using namespace BetterEnchantmentEffects;
using test::Check;
int main() {
  TargetPool pool{2};
  auto retained = pool.Acquire();
  Check(retained && *retained == 0, "first lease reserves a presenter");
  bool reused = true;
  for (int i = 0; i < 1024; ++i) {
    auto transient = pool.Acquire();
    reused &= transient && *transient == 1 && !pool.Acquire();
  }
  Check(reused,
        "repeated destruction reuses free slots without reusing retained ones");
  auto snapshot = retained;
  retained.reset();
  auto second = pool.Acquire();
  Check(second && *second == 1 && !pool.Acquire(),
        "snapshot keeps its slot reserved");
  snapshot.reset();
  auto first = pool.Acquire();
  Check(first && *first == 0,
        "last consumer release returns the presenter slot");
  std::shared_ptr<const std::size_t> outstanding;
  {
    TargetPool temporary{1};
    outstanding = temporary.Acquire();
  }
  Check(outstanding && *outstanding == 0, "lease survives inventory teardown");
  return test::Finish("planners targetpool");
}
