// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kCarryWindowMS = 2000;
inline constexpr std::size_t kMaxCarriedInstanceTimes = 4096;

struct CarriedTimeKey {
  std::uint32_t actorID = 0;
  std::string recipeID;
  std::uint32_t enchantmentID = 0;
  [[nodiscard]] std::strong_ordering
  operator<=>(const CarriedTimeKey &) const = default;
};

[[nodiscard]] std::optional<std::uint32_t>
ClockOffsetMS(float a_seconds, float a_speed) noexcept;

class CarriedTimes {
public:
  void Remember(CarriedTimeKey a_key, float a_seconds, std::uint32_t a_nowMS);
  [[nodiscard]] std::optional<float> Take(const CarriedTimeKey &a_key,
                                          std::uint32_t a_nowMS);
  void Expire(std::uint32_t a_nowMS);
  void Clear() noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;

private:
  struct Entry {
    float seconds = 0.0f;
    std::uint32_t retiredMS = 0;
  };
  std::map<CarriedTimeKey, Entry> entries_;
};
}
