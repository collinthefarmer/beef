// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"

#include <cstdint>
#include <string>

namespace BetterEnchantmentEffects {
void CancelRegression();
[[nodiscard]] std::int32_t SubmitRegressionRequest(RE::Actor *a_actor,
                                                   bool a_retire);
[[nodiscard]] std::string RegressionResult(std::int32_t a_request);
void AbortRegressionRequest(std::int32_t a_request);
void SoloRecipeUnderTest(std::string a_recipe);
void RestoreRecipeView();
[[nodiscard]] RE::TESObjectARMO *RegressionFixture();
}
