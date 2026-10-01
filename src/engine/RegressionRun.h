// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <string_view>

namespace BetterEnchantmentEffects {
struct RunSetup {
  bool effectsEnabled = false;
  std::string_view build;
  std::string_view source;
};

void ReadRegressionRun(const RunSetup &a_setup);
void FinishRegressionLoad(bool a_loaded);
void AdvanceRegressionRun();
}
