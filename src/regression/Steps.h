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
  const Ownership &owned;
  const std::array<std::uint64_t, kRoleCount> &renderMarks;
};

struct StepMove {
  Verdict verdict;
  std::optional<Command> command;
  Ownership owned;
};

[[nodiscard]] StepMove StartStep(const Step &a_step,
                                 const StepContext &a_context);
[[nodiscard]] StepMove CheckStep(const Step &a_step,
                                 const StepContext &a_context);
[[nodiscard]] std::array<bool, kRoleCount> RolesMoved(const Step &a_step);
}
