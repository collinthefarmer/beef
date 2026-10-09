// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/Hold.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <optional>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint64_t kNoByteBound =
    std::numeric_limits<std::uint64_t>::max();

struct RetentionPolicy {
  std::size_t maxCount = 0;
  std::uint64_t maxBytes = 0;
  Millis maxIdle = 0;
};

struct KeepNone {
  template <class Key, class Value>
  [[nodiscard]] constexpr bool operator()(const Key &,
                                          const Value &) const noexcept {
    return false;
  }
};

inline constexpr Millis kStepOutputIdleMs = 500;

enum class Retention { kRenew, kKeep, kEvict };

[[nodiscard]] constexpr Retention
StepOutputRetention(bool a_heldElsewhere, Millis a_idleMs) noexcept {
  if (a_heldElsewhere) {
    return Retention::kRenew;
  }
  return a_idleMs > kStepOutputIdleMs ? Retention::kEvict : Retention::kKeep;
}

template <class Key, class Value> class RetainedStore {
public:
  using Evicted = std::vector<Value>;

  explicit RetainedStore(RetentionPolicy a_policy) : policy_(a_policy) {}

  [[nodiscard]] const Value *Find(const Key &a_key) const {
    const auto found = std::ranges::find(entries_, a_key, &Entry::key);
    return found == entries_.end() ? nullptr : &found->value;
  }

  [[nodiscard]] Value *Use(const Key &a_key, Millis a_now) {
    const auto found = std::ranges::find(entries_, a_key, &Entry::key);
    if (found == entries_.end()) {
      return nullptr;
    }
    found->lastUsed = a_now;
    return &found->value;
  }

  Value &Get(const Key &a_key, Millis a_now) {
    const auto found = std::ranges::find(entries_, a_key, &Entry::key);
    if (found != entries_.end()) {
      found->lastUsed = a_now;
      return found->value;
    }
    return entries_.emplace_back(Entry{a_key, Value{}, 0, a_now}).value;
  }

  [[nodiscard]] std::optional<Value>
  Insert(Key a_key, Value a_value, std::uint64_t a_bytes, Millis a_now) {
    if (policy_.maxCount == 0 || a_bytes > policy_.maxBytes) {
      return std::optional<Value>{std::move(a_value)};
    }
    entries_.emplace_back(
        Entry{std::move(a_key), std::move(a_value), a_bytes, a_now});
    bytes_ += a_bytes;
    return std::nullopt;
  }

  [[nodiscard]] std::optional<Value> Take(const Key &a_key) {
    const auto found = std::ranges::find(entries_, a_key, &Entry::key);
    if (found == entries_.end()) {
      return std::nullopt;
    }
    std::optional<Value> taken{std::move(found->value)};
    bytes_ -= found->bytes;
    entries_.erase(found);
    return taken;
  }

  template <class Keep>
  [[nodiscard]] Evicted Sweep(Millis a_now, const Keep &a_keep) {
    Evicted evicted;
    std::vector<typename std::list<Entry>::iterator> unkept;
    std::size_t unkeptCount = 0;
    std::uint64_t unkeptBytes = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
      if (a_keep(it->key, it->value)) {
        it->lastUsed = a_now;
        ++it;
        continue;
      }
      it->lastUsed = std::min(it->lastUsed, a_now);
      if (a_now - it->lastUsed >= policy_.maxIdle) {
        it = Evict(it, evicted);
        continue;
      }
      unkept.push_back(it);
      ++unkeptCount;
      unkeptBytes += it->bytes;
      ++it;
    }
    std::ranges::stable_sort(
        unkept, {}, [](const auto &a_entry) { return a_entry->lastUsed; });
    for (auto oldest = unkept.begin();
         oldest != unkept.end() &&
         (unkeptCount > policy_.maxCount || unkeptBytes > policy_.maxBytes);
         ++oldest) {
      --unkeptCount;
      unkeptBytes -= (*oldest)->bytes;
      Evict(*oldest, evicted);
    }
    return evicted;
  }

  [[nodiscard]] Evicted Clear() {
    Evicted evicted;
    evicted.reserve(entries_.size());
    for (Entry &entry : entries_) {
      evicted.push_back(std::move(entry.value));
    }
    entries_.clear();
    bytes_ = 0;
    return evicted;
  }

  template <class Visit> void ForEach(const Visit &a_visit) const {
    for (const Entry &entry : entries_) {
      a_visit(entry.value);
    }
  }

  [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
  [[nodiscard]] std::uint64_t Bytes() const noexcept { return bytes_; }

private:
  struct Entry {
    Key key;
    Value value;
    std::uint64_t bytes = 0;
    Millis lastUsed = 0;
  };

  typename std::list<Entry>::iterator
  Evict(typename std::list<Entry>::iterator a_entry, Evicted &a_evicted) {
    a_evicted.push_back(std::move(a_entry->value));
    bytes_ -= a_entry->bytes;
    return entries_.erase(a_entry);
  }

  RetentionPolicy policy_;
  std::uint64_t bytes_ = 0;
  std::list<Entry> entries_;
};
}
