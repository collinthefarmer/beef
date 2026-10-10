// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects {
struct OutputInactive {
  std::string problem;
  [[nodiscard]] bool operator==(const OutputInactive &) const = default;
};

struct OutputPending {
  [[nodiscard]] bool operator==(const OutputPending &) const = default;
};

struct OutputRendered {
  [[nodiscard]] bool operator==(const OutputRendered &) const = default;
};

struct OutputFailed {
  std::string problem;
  [[nodiscard]] bool operator==(const OutputFailed &) const = default;
};

using OutputState =
    std::variant<OutputInactive, OutputPending, OutputRendered, OutputFailed>;

inline constexpr std::string_view kRenderFailedProblem =
    "the output could not be rendered; reapply to retry";
inline constexpr std::string_view kRetiredProblem =
    "the geometry was retired after ownership changed";

enum class OutputRender { kRendered, kPending, kFailed };

[[nodiscard]] std::string_view ProblemOf(const OutputState &a_state) noexcept;
[[nodiscard]] bool IsActive(const OutputState &a_state) noexcept;
[[nodiscard]] OutputState Activated(std::string a_problem);
[[nodiscard]] OutputState AfterRender(const OutputState &a_state,
                                      OutputRender a_render);
[[nodiscard]] OutputState AfterLostGeometry(const OutputState &a_state);
[[nodiscard]] OutputState WithProblem(const OutputState &a_state,
                                      std::string a_problem);
[[nodiscard]] OutputState AfterRetire(const OutputState &a_state);
}
