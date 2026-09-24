// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxWarningKeys = 1024;
inline constexpr std::size_t kMaxWarningKeyBytes = 128u * 1024u;

enum class WarningDecision {
  kFirst,
  kRepeat,
  kLimit,
  kOmitted,
};

struct WarningHistoryStatus {
  std::size_t keys = 0;
  std::size_t bytes = 0;
};

class WarningHistory {
public:
  [[nodiscard]] WarningDecision Observe(std::string_view a_key);
  void Clear();
  [[nodiscard]] WarningHistoryStatus Inspect() const;

private:
  mutable std::mutex lock_;
  std::set<std::string, std::less<>> seen_;
  std::size_t bytes_ = 0;
  bool limitReported_ = false;
};
}
