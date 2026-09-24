// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <functional>
#include <memory>

namespace BetterEnchantmentEffects {
struct SlotTarget {
  virtual ~SlotTarget() = default;
};
struct MaterialBinding : SlotTarget {
  std::function<void()> onDestroy;
  ~MaterialBinding() override {
    if (onDestroy)
      onDestroy();
  }
};
struct ShellBinding : SlotTarget {
  std::function<void()> onDestroy;
  ~ShellBinding() override {
    if (onDestroy)
      onDestroy();
  }
};
struct LightBinding {};
}
