// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Run.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

namespace BetterEnchantmentEffects {
enum class RequestKind : std::uint8_t { kApply, kRetire };

struct RegressionRequest {
  std::uint64_t id = 0;
  std::uint32_t actor = 0;
  RequestKind kind = RequestKind::kApply;
  std::uint64_t previousAttempt = 0;
  bool dispatched = false;
  Regression::RequestState state = Regression::NoRequest{};
};

[[nodiscard]] inline bool Pending(const RegressionRequest &a_request) {
  return std::holds_alternative<Regression::RequestPending>(a_request.state);
}

[[nodiscard]] inline bool Pending(const RegressionRequest &a_request,
                                  std::uint64_t a_id) {
  return a_id == a_request.id && Pending(a_request);
}

[[nodiscard]] inline std::optional<std::uint64_t>
BeginRequest(RegressionRequest &a_request, std::uint32_t a_actor,
             RequestKind a_kind) {
  if (a_actor == 0 || Pending(a_request) ||
      a_request.id == std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }
  a_request = RegressionRequest{.id = a_request.id + 1,
                                .actor = a_actor,
                                .kind = a_kind,
                                .state = Regression::RequestPending{}};
  return a_request.id;
}

[[nodiscard]] inline Regression::RequestState
StateOf(const RegressionRequest &a_request, std::uint64_t a_id) {
  return a_id == a_request.id
             ? a_request.state
             : Regression::RequestState{Regression::Outcome::kAborted};
}
}
