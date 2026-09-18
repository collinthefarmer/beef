#include "studio/ApplicationRecord.h"

#include "Core.h"

#include <array>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::array<std::string_view, kApplicationPhaseCount> kPhaseNames{
    "queued", "prepared", "rendered", "failed", "unmatched", "cancelled"};
}

std::string_view ApplicationPhaseName(ApplicationPhase a_phase) noexcept {
  const auto index = IndexOf(a_phase);
  return index < kPhaseNames.size() ? kPhaseNames[index] : "unknown";
}
}
