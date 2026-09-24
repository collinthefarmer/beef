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

void CarriedTimes::Remember(CarriedTimeKey a_key, float a_seconds,
                            std::uint32_t a_nowMS) {
  if (!std::isfinite(a_seconds) || a_seconds < 0.0f) {
    entries_.erase(a_key);
    return;
  }
  if (const auto existing = entries_.find(a_key); existing != entries_.end()) {
    existing->second = Entry{a_seconds, a_nowMS};
    return;
  }
  if (entries_.size() >= kMaxCarriedInstanceTimes) {
    return;
  }
  entries_.emplace(std::move(a_key), Entry{a_seconds, a_nowMS});
}

std::optional<float> CarriedTimes::Take(const CarriedTimeKey &a_key,
                                        std::uint32_t a_nowMS) {
  const auto found = entries_.find(a_key);
  if (found == entries_.end()) {
    return std::nullopt;
  }
  const Entry entry = found->second;
  entries_.erase(found);
  return a_nowMS - entry.retiredMS <= kCarryWindowMS
             ? std::optional<float>{entry.seconds}
             : std::nullopt;
}

void CarriedTimes::Expire(std::uint32_t a_nowMS) {
  std::erase_if(
      entries_,
      [a_nowMS](const std::pair<const CarriedTimeKey, Entry> &a_entry) {
        return a_nowMS - a_entry.second.retiredMS > kCarryWindowMS;
      });
}

void CarriedTimes::Clear() noexcept { entries_.clear(); }

std::size_t CarriedTimes::Size() const noexcept { return entries_.size(); }
}
