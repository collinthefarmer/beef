#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects {
struct TransformStorage {
  std::uintptr_t entries = 0;
  std::size_t count = 0;
  std::size_t stride = 0;
  std::size_t offset = 0;
};

[[nodiscard]] std::optional<std::size_t>
TransformStorageIndex(std::uintptr_t a_address,
                      const TransformStorage &a_storage) noexcept;
}
