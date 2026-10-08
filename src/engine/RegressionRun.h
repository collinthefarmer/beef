// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
struct RunSetup {
  bool effectsEnabled = false;
  std::string_view build;
  std::string_view source;
  bool screenResolution = true;
};

void ReadRegressionRun(const RunSetup &a_setup);
void FinishRegressionLoad(bool a_loaded);
void AdvanceRegressionRun();
[[nodiscard]] bool RegressionVerifiesRenderPlans() noexcept;
[[nodiscard]] std::optional<bool> RegressionScreenResolution() noexcept;
}
