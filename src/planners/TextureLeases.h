// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace BetterEnchantmentEffects {
template <class Target> struct TextureLeaseLookup {
  bool generated = false;
  std::uint64_t generation = 0;
  std::shared_ptr<Target> target;
};

template <class Target> class TextureLeases {
public:
  bool Register(std::uintptr_t a_presenter, std::uint64_t a_generation,
                const std::shared_ptr<Target> &a_target) {
    if (!a_presenter || !a_generation || !a_target) {
      return false;
    }
    std::scoped_lock lock{lock_};
    if (const auto it = entries_.find(a_presenter); it != entries_.end()) {
      if (const auto live = it->second.target.lock()) {
        return live == a_target && it->second.generation == a_generation;
      }
    }
    entries_[a_presenter] = Entry{a_generation, a_target};
    return true;
  }

  [[nodiscard]] TextureLeaseLookup<Target> Retain(std::uintptr_t a_presenter) {
    std::scoped_lock lock{lock_};
    const auto it = entries_.find(a_presenter);
    if (it == entries_.end()) {
      return {};
    }
    return {true, it->second.generation, it->second.target.lock()};
  }

private:
  struct Entry {
    std::uint64_t generation = 0;
    std::weak_ptr<Target> target;
  };
  std::mutex lock_;
  std::unordered_map<std::uintptr_t, Entry> entries_;
};
}
