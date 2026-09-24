// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/OwnedState.h"
#include "test_support.h"
#include <cstdint>
#include <memory>
using namespace BetterEnchantmentEffects;
using test::Check;
namespace {
struct Group {
  std::shared_ptr<int> texture;
  float strength = 0;
  std::uint32_t features = 0;
  bool operator==(const Group &) const = default;
};
}
int main() {
  Group original{std::make_shared<int>(1), 2, 0};
  OwnedState material{original};
  Group live{std::make_shared<int>(3), 4, 1};
  const std::weak_ptr<int> generated = live.texture;
  material.Written(live);
  Check(material.Restore(live) == original,
        "owned group restores its complete baseline");
  live.strength = 7;
  Check(!material.Restore(live),
        "external scalar change preserves the coupled texture and flags");
  OwnedState unrelated{5};
  unrelated.Written(9);
  Check(unrelated.Restore(9) == 5,
        "external takeover does not block unrelated restoration");
  auto retained = live.texture;
  live.texture.reset();
  Check(!generated.expired(),
        "published texture lease survives producer release");
  Group moved = material.LastWritten();
  moved.texture = std::make_shared<int>(8);
  Check(!material.Restore(moved),
        "replacement texture prevents group restoration");
  moved = material.LastWritten();
  moved.features ^= 1;
  Check(!material.Restore(moved),
        "owned feature-bit changes prevent group restoration");
  return test::Finish("owned state");
}
