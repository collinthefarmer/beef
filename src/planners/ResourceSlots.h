#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace BetterEnchantmentEffects {
// A slot is available again only after its last owner releases the lease.
// Outstanding leases can outlive the inventory.
class ResourceSlots {
public:
  explicit ResourceSlots(std::size_t a_count) : slots_(a_count) {}
  [[nodiscard]] std::shared_ptr<const std::size_t> Acquire() {
    std::scoped_lock lock{lock_};
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      if (!slots_[i].expired())
        continue;
      auto lease = std::make_shared<const std::size_t>(i);
      slots_[i] = lease;
      return lease;
    }
    return {};
  }

private:
  std::mutex lock_;
  std::vector<std::weak_ptr<const std::size_t>> slots_;
};
}
