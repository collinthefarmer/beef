#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
template <class Key, class Value> class RetainedCache {
public:
  Value &Get(const Key &a_key, std::uint32_t a_nowMS) {
    Entry &entry = entries_[a_key];
    entry.lastUsedMS = a_nowMS;
    return entry.value;
  }

  [[nodiscard]] const Value *Find(const Key &a_key) const {
    const auto found = entries_.find(a_key);
    return found == entries_.end() ? nullptr : &found->second.value;
  }

  void Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS,
             std::size_t a_maxUnused, std::span<const Key> a_keep) {
    std::vector<std::pair<std::uint32_t, Key>> unused;
    for (auto it = entries_.begin(); it != entries_.end();) {
      auto &[key, entry] = *it;
      if (std::ranges::contains(a_keep, key)) {
        entry.lastUsedMS = a_nowMS;
        ++it;
        continue;
      }
      const std::uint32_t age = a_nowMS - entry.lastUsedMS;
      if (age >= a_maxAgeMS) {
        it = entries_.erase(it);
      } else {
        unused.emplace_back(age, key);
        ++it;
      }
    }
    std::ranges::sort(unused, [](const auto &a_left, const auto &a_right) {
      return a_left.first > a_right.first;
    });
    const std::size_t excess =
        unused.size() > a_maxUnused ? unused.size() - a_maxUnused : 0;
    for (std::size_t i = 0; i < excess; ++i) {
      entries_.erase(unused[i].second);
    }
  }

  void Clear() noexcept { entries_.clear(); }
  [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

private:
  struct Entry {
    Value value;
    std::uint32_t lastUsedMS = 0;
  };
  std::map<Key, Entry> entries_;
};
}
