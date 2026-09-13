#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace BetterEnchantmentEffects {
// The consumer acknowledges submission, rather than estimating completion from
// elapsed ticks. Unacknowledged work remains retained, including across clears.
template <class Resource> class ConsumptionLeases {
public:
  class Ticket {
  public:
    void Consumed() noexcept {
      consumed_.store(true, std::memory_order_release);
    }

  private:
    friend class ConsumptionLeases;
    explicit Ticket(std::shared_ptr<Resource> a_resource)
        : resource_(std::move(a_resource)) {}
    std::shared_ptr<Resource> resource_;
    std::atomic<bool> consumed_{false};
  };
  explicit ConsumptionLeases(std::size_t a_limit) : limit_(a_limit) {}
  [[nodiscard]] Ticket *Retain(std::shared_ptr<Resource> a_resource) {
    if (!a_resource)
      return nullptr;
    std::scoped_lock lock{lock_};
    CollectLocked();
    if (pending_.size() >= limit_)
      return nullptr;
    auto ticket = std::unique_ptr<Ticket>{new Ticket{std::move(a_resource)}};
    auto *result = ticket.get();
    pending_.push_back(std::move(ticket));
    return result;
  }
  void Collect() {
    std::scoped_lock lock{lock_};
    CollectLocked();
  }

private:
  void CollectLocked() {
    std::erase_if(pending_, [](const auto &a_ticket) {
      return a_ticket->consumed_.load(std::memory_order_acquire);
    });
  }
  std::mutex lock_;
  std::size_t limit_;
  std::vector<std::unique_ptr<Ticket>> pending_;
};
}
