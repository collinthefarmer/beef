// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "engine/RegressionRequest.h"

#include <cstdint>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
void AbortWaitingRegressionRequest();
[[nodiscard]] std::optional<std::uint64_t>
SubmitRegressionRequest(RE::Actor *a_actor, RequestKind a_kind);
[[nodiscard]] Regression::RequestState
RegressionRequestState(std::optional<std::uint64_t> a_request);
void AbortRegressionRequest(std::optional<std::uint64_t> a_request);
void SoloRegressionRecipe(std::string a_recipe);
void EndRegressionSolo();
[[nodiscard]] RE::TESObjectARMO *RegressionFixture();
}
