// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/InstanceTime.h"

#include <cmath>
#include <limits>
#include <utility>

namespace BetterEnchantmentEffects {
std::optional<std::uint32_t> ClockOffsetMS(float a_seconds,
                                           float a_speed) noexcept {
  if (!std::isfinite(a_seconds) || a_seconds < 0.0f ||
      !std::isfinite(a_speed) || a_speed <= 0.0f) {
    return std::nullopt;
  }
  const double milliseconds =
      static_cast<double>(a_seconds) / static_cast<double>(a_speed) * 1000.0;
  if (!std::isfinite(milliseconds) ||
      milliseconds > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(milliseconds);
}

void CarriedInstances::Remember(CarriedInstanceKey a_key,
                                CarriedInstance a_instance,
                                std::uint32_t a_nowMS) {
  if (!std::isfinite(a_instance.seconds) || a_instance.seconds < 0.0f) {
    entries_.erase(a_key);
    return;
  }
  if (const auto existing = entries_.find(a_key); existing != entries_.end()) {
    existing->second = Entry{std::move(a_instance), a_nowMS};
    return;
  }
  if (entries_.size() >= kMaxCarriedInstances) {
    return;
  }
  entries_.emplace(std::move(a_key), Entry{std::move(a_instance), a_nowMS});
}

std::optional<CarriedInstance>
CarriedInstances::Take(const CarriedInstanceKey &a_key, std::uint32_t a_nowMS) {
  const auto found = entries_.find(a_key);
  if (found == entries_.end()) {
    return std::nullopt;
  }
  Entry entry = std::move(found->second);
  entries_.erase(found);
  if (a_nowMS - entry.retiredMS > kCarryWindowMS) {
    return std::nullopt;
  }
  return std::move(entry.instance);
}

void CarriedInstances::Expire(std::uint32_t a_nowMS) {
  std::erase_if(
      entries_,
      [a_nowMS](const std::pair<const CarriedInstanceKey, Entry> &a_entry) {
        return a_nowMS - a_entry.second.retiredMS > kCarryWindowMS;
      });
}

void CarriedInstances::Clear() noexcept { entries_.clear(); }

std::size_t CarriedInstances::Size() const noexcept { return entries_.size(); }
}
