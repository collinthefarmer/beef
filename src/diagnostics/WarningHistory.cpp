// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/WarningHistory.h"

namespace BetterEnchantmentEffects {
WarningDecision WarningHistory::Observe(std::string_view a_key) {
  const std::scoped_lock guard{lock_};
  if (seen_.find(a_key) != seen_.end()) {
    return WarningDecision::kRepeat;
  }
  if (a_key.size() > kMaxWarningKeyBytes - bytes_ ||
      seen_.size() >= kMaxWarningKeys) {
    if (limitReported_) {
      return WarningDecision::kOmitted;
    }
    limitReported_ = true;
    return WarningDecision::kLimit;
  }
  seen_.emplace(a_key);
  bytes_ += a_key.size();
  return WarningDecision::kFirst;
}

void WarningHistory::Clear() {
  const std::scoped_lock guard{lock_};
  seen_.clear();
  bytes_ = 0;
  limitReported_ = false;
}

WarningHistoryStatus WarningHistory::Inspect() const {
  const std::scoped_lock guard{lock_};
  return {seen_.size(), bytes_};
}
}
