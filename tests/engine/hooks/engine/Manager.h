// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"

namespace BetterEnchantmentEffects {
class Manager {
public:
  static Manager *GetSingleton() {
    static Manager manager;
    return &manager;
  }
  void OnFrame() { HookFixture::calls.push_back(2); }
};
}
