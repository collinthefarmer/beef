// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "recipe/Signals.h"

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kCarryWindowMS = 2000;
inline constexpr std::size_t kMaxCarriedInstances = 4096;

struct CarriedInstanceKey {
  std::uint32_t actorID = 0;
  std::string recipeID;
  std::uint32_t enchantmentID = 0;
  std::string effectScope{};
  [[nodiscard]] std::strong_ordering
  operator<=>(const CarriedInstanceKey &) const = default;
};

struct CarriedInstance {
  float seconds = 0.0f;
  std::vector<SignalMemoryEntry> signals;
};

[[nodiscard]] std::optional<std::uint32_t>
ClockOffsetMS(float a_seconds, float a_speed) noexcept;

class CarriedInstances {
public:
  void Remember(CarriedInstanceKey a_key, CarriedInstance a_instance,
                std::uint32_t a_nowMS);
  [[nodiscard]] std::optional<CarriedInstance>
  Take(const CarriedInstanceKey &a_key, std::uint32_t a_nowMS);
  void Expire(std::uint32_t a_nowMS);
  void Clear() noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;

private:
  struct Entry {
    CarriedInstance instance;
    std::uint32_t retiredMS = 0;
  };
  std::map<CarriedInstanceKey, Entry> entries_;
};
}
