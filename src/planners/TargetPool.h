// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace BetterEnchantmentEffects {
class TargetPool {
public:
  explicit TargetPool(std::size_t a_count) : leases_(a_count) {}
  [[nodiscard]] std::shared_ptr<const std::size_t> Acquire() {
    std::scoped_lock lock{lock_};
    for (std::size_t i = 0; i < leases_.size(); ++i) {
      if (!leases_[i].expired())
        continue;
      auto lease = std::make_shared<const std::size_t>(i);
      leases_[i] = lease;
      return lease;
    }
    return {};
  }

private:
  std::mutex lock_;
  std::vector<std::weak_ptr<const std::size_t>> leases_;
};
}
