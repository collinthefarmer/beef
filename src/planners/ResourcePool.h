// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
template <class T> class ResourcePool {
public:
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

  bool Retain(std::unique_ptr<T> a_value, std::uint64_t a_bytes) {
    if (!a_value || entries_.size() >= maxCount_ ||
        a_bytes > maxBytes_ - bytes_) {
      return false;
    }
    entries_.push_back({std::move(a_value), a_bytes});
    bytes_ += a_bytes;
    return true;
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
  };
  std::size_t maxCount_;
  std::uint64_t maxBytes_;
  std::uint64_t bytes_ = 0;
  std::vector<Entry> entries_;
};
}
