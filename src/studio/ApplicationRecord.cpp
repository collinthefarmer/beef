#include "studio/ApplicationRecord.h"

#include <array>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::array<std::string_view, kApplicationPhaseCount> kPhaseNames{
    "queued", "prepared", "rendered", "failed", "unmatched", "cancelled"};
}

std::string_view ApplicationPhaseName(ApplicationPhase a_phase) noexcept {
  const auto index = static_cast<std::size_t>(a_phase);
  return index < kPhaseNames.size() ? kPhaseNames[index] : "unknown";
}
}
