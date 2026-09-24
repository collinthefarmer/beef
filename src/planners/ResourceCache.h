#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects {
template <class T> struct SharedResource {
  std::shared_ptr<T> value;
  bool adopted = false;
};

template <class T> class ResourceCache {
public:
  template <class Make>
  [[nodiscard]] SharedResource<T> Adopt(const std::string &a_key,
                                        Make &&a_make) {
    Sweep();
    if (const auto found = entries_.find(a_key); found != entries_.end()) {
      if (std::shared_ptr<T> live = found->second.lock()) {
        return {std::move(live), true};
      }
    }
    std::shared_ptr<T> made = a_make();
    if (made) {
      entries_[a_key] = made;
    }
    return {std::move(made), false};
  }

  void Sweep() {
    std::erase_if(entries_,
                  [](const auto &a_entry) { return a_entry.second.expired(); });
  }

  [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

  void Clear() noexcept { entries_.clear(); }

  [[nodiscard]] std::size_t LiveCount() const noexcept {
    std::size_t live = 0;
    for (const auto &[key, weak] : entries_) {
      if (!weak.expired()) {
        ++live;
      }
    }
    return live;
  }

private:
  std::unordered_map<std::string, std::weak_ptr<T>> entries_;
};
}
