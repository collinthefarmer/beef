// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
enum class ApplicationPhase {
  kQueued,
  kPrepared,
  kRendered,
  kFailed,
  kUnmatched,
  kCancelled,
};
inline constexpr std::size_t kApplicationPhaseCount = 6;

struct ApplicationToken {
  std::string recipeID;
  std::uint64_t revision = 0;
  std::uint32_t actorID = 0;
  std::uint64_t attempt = 0;
  [[nodiscard]] bool operator==(const ApplicationToken &) const = default;
};

struct ApplicationActor {
  std::uint32_t actorID = 0;
  ApplicationPhase phase = ApplicationPhase::kQueued;
  std::string problem;
  std::uint64_t attempt = 0;
};

struct ApplicationRecord {
  ApplicationToken token;
  ApplicationPhase phase = ApplicationPhase::kQueued;
  std::vector<ApplicationActor> actors;
  std::string problem;
};

[[nodiscard]] std::string_view
ApplicationPhaseName(ApplicationPhase a_phase) noexcept;
}
