// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
template <class T> class ResourcePool {
public:
  using Released = std::vector<std::unique_ptr<T>>;

  ResourcePool(std::size_t a_maxCount, std::uint64_t a_maxBytes)
      : maxCount_(a_maxCount), maxBytes_(a_maxBytes) {}

  template <class Matches>
  [[nodiscard]] std::unique_ptr<T> Take(Matches a_matches) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (a_matches(*it->value)) {
        auto value = std::move(it->value);
        bytes_ -= it->bytes;
        entries_.erase(it);
        return value;
      }
    }
    return nullptr;
  }

  [[nodiscard]] Released Retain(std::unique_ptr<T> a_value,
                                std::uint64_t a_bytes) {
    Released released;
    if (!a_value) {
      return released;
    }
    if (maxCount_ == 0 || a_bytes > maxBytes_) {
      released.push_back(std::move(a_value));
      return released;
    }
    while (!entries_.empty() &&
           (entries_.size() >= maxCount_ || a_bytes > maxBytes_ - bytes_)) {
      released.push_back(std::move(entries_.front().value));
      bytes_ -= entries_.front().bytes;
      entries_.erase(entries_.begin());
    }
    entries_.push_back({std::move(a_value), a_bytes, std::nullopt});
    bytes_ += a_bytes;
    return released;
  }

  [[nodiscard]] Released Expire(std::uint64_t a_nowMs,
                                std::uint64_t a_maxIdleMs) {
    Released released;
    std::vector<Entry> kept;
    kept.reserve(entries_.size());
    for (Entry &entry : entries_) {
      const std::uint64_t since = entry.idleSince.value_or(a_nowMs);
      if (a_nowMs >= since && a_nowMs - since >= a_maxIdleMs) {
        bytes_ -= entry.bytes;
        released.push_back(std::move(entry.value));
      } else {
        entry.idleSince = since;
        kept.push_back(std::move(entry));
      }
    }
    entries_ = std::move(kept);
    return released;
  }

  void Clear() noexcept {
    entries_.clear();
    bytes_ = 0;
  }
  [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::uint64_t Bytes() const noexcept { return bytes_; }

private:
  struct Entry {
    std::unique_ptr<T> value;
    std::uint64_t bytes = 0;
    std::optional<std::uint64_t> idleSince;
  };
  std::size_t maxCount_;
  std::uint64_t maxBytes_;
  std::uint64_t bytes_ = 0;
  std::vector<Entry> entries_;
};
}
