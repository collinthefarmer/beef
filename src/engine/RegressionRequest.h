// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace BetterEnchantmentEffects {
struct RegressionRequest {
  std::int32_t id = 0;
  std::uint32_t actor = 0;
  std::uint64_t previousAttempt = 0;
  bool retire = false;
  bool dispatched = false;
  std::string result = "IDLE";

  [[nodiscard]] std::int32_t Begin(std::uint32_t a_actor, bool a_retire) {
    if (a_actor == 0 || result == "WAITING" ||
        id == std::numeric_limits<std::int32_t>::max())
      return 0;
    ++id;
    actor = a_actor;
    retire = a_retire;
    dispatched = false;
    previousAttempt = 0;
    result = "WAITING";
    return id;
  }

  [[nodiscard]] bool Pending(std::int32_t a_id) const {
    return a_id == id && result == "WAITING";
  }

  [[nodiscard]] std::string Result(std::int32_t a_id) const {
    return a_id > 0 && a_id == id ? result : "ABORTED";
  }

  void Cancel() { result = "ABORTED"; }
};
}
