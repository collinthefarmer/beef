#include "planners/TransformStorage.h"

namespace BetterEnchantmentEffects {
std::optional<std::size_t>
TransformStorageIndex(std::uintptr_t a_address,
                      const TransformStorage &a_storage) noexcept {
  if (!a_address || !a_storage.entries || !a_storage.stride ||
      a_storage.offset >= a_storage.stride || a_address < a_storage.entries) {
    return std::nullopt;
  }
  const auto distance = a_address - a_storage.entries;
  if (distance < a_storage.offset ||
      (distance - a_storage.offset) % a_storage.stride != 0) {
    return std::nullopt;
  }
  const auto index = (distance - a_storage.offset) / a_storage.stride;
  return index < a_storage.count ? std::optional<std::size_t>{index}
                                 : std::nullopt;
}
}
