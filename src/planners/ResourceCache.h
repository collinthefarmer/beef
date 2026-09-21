#pragma once

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
    if (std::shared_ptr<T> live = entries_[a_key].lock()) {
      return {std::move(live), true};
    }
    std::shared_ptr<T> made = a_make();
    if (made) {
      entries_[a_key] = made;
    }
    return {std::move(made), false};
  }

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
