// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Steps.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Regression {
struct RunFile {
  std::string run;
  std::string save;
  CaseList suite;
  bool verifyRenderPlans = false;
  std::optional<std::uint32_t> measuredSeconds;
  std::optional<bool> screenResolution;
};

inline constexpr std::size_t kMaxSuiteCases = 64;
inline constexpr std::uint32_t kMinMeasuredSeconds = 10;
inline constexpr std::uint32_t kMaxMeasuredSeconds = 3600;

[[nodiscard]] std::expected<RunFile, std::string>
ParseRunFile(std::string_view a_text, std::int64_t a_nowSeconds);
}
