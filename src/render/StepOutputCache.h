// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "render/TextureRef.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects {
class StepOutputCache {
public:
  [[nodiscard]] std::optional<TextureView> Find(const std::string &a_key,
                                                std::uint64_t a_nowMS);
  void Publish(const std::string &a_key, const TextureView &a_view,
               std::uint64_t a_nowMS);
  void Sweep(std::uint64_t a_nowMS);
  void Clear() noexcept;

private:
  struct Entry {
    TextureView view;
    std::uint64_t lastUsedMS = 0;
  };
  std::unordered_map<std::string, Entry> entries_;
};
}
