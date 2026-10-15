// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/OutputState.h"

#include "Core.h"

#include <utility>

namespace BetterEnchantmentEffects {
std::string_view ProblemOf(const OutputState &a_state) noexcept {
  if (const OutputInactive *inactive = Get<OutputInactive>(a_state)) {
    return inactive->problem;
  }
  if (const OutputFailed *failed = Get<OutputFailed>(a_state)) {
    return failed->problem;
  }
  return {};
}

std::string_view SkipReasonOf(const OutputState &a_state) noexcept {
  const OutputSkipped *skipped = Get<OutputSkipped>(a_state);
  return skipped ? std::string_view{skipped->reason} : std::string_view{};
}

bool IsActive(const OutputState &a_state) noexcept {
  return !Is<OutputInactive>(a_state) && !Is<OutputSkipped>(a_state);
}

OutputState Activated(std::string a_problem) {
  if (a_problem.empty()) {
    return OutputPending{};
  }
  return OutputFailed{std::move(a_problem)};
}

OutputState AfterRender(const OutputState &a_state, OutputRender a_render) {
  if (!IsActive(a_state)) {
    return a_state;
  }
  switch (a_render) {
  case OutputRender::kRendered:
    return OutputRendered{};
  case OutputRender::kPending:
    return OutputPending{};
  case OutputRender::kFailed:
    return OutputFailed{std::string{kRenderFailedProblem}};
  }
  return a_state;
}

OutputState AfterLostGeometry(const OutputState &a_state) {
  if (!IsActive(a_state)) {
    return a_state;
  }
  return OutputFailed{std::string{kRenderFailedProblem}};
}

OutputState WithProblem(const OutputState &a_state, std::string a_problem) {
  if (!IsActive(a_state)) {
    return OutputInactive{std::move(a_problem)};
  }
  return OutputFailed{std::move(a_problem)};
}

OutputState AfterRetire(const OutputState &a_state) {
  const std::string_view problem = ProblemOf(a_state);
  return WithProblem(a_state,
                     std::string{problem.empty() ? kRetiredProblem : problem});
}
}
