// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Run.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects::Regression {
struct Pending {};
struct Completed {
  Outcome outcome = Outcome::kPass;
  std::string reason;
};
using Verdict = std::variant<Pending, Completed>;

struct StepContext {
  const Observation &seen;
  std::uint32_t frames = 0;
  std::uint64_t startedAtMs = 0;
  std::uint32_t loadsAtStart = 0;
  const RunChanges &changes;
  const std::array<std::uint64_t, kRoleCount> &renderedBefore;
};

struct StepMove {
  Verdict verdict;
  std::optional<Command> command;
};

[[nodiscard]] StepMove StartStep(const Step &a_step,
                                 const StepContext &a_context);
[[nodiscard]] StepMove CheckStep(const Step &a_step,
                                 const StepContext &a_context);
[[nodiscard]] std::array<bool, kRoleCount> RolesAffected(const Step &a_step);
}
