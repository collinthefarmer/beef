#pragma once

#include "recipe/Signals.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kPluginEventMessage = 0x42454546;
inline constexpr std::uint32_t kPluginEventVersion = 1;
inline constexpr std::size_t kPluginEventIdMax = 64;

enum class PluginEventType : std::uint32_t {
  kScalar = 1,
  kVec2 = 2,
  kVec3 = 3,
};

struct PluginEventMessage {
  std::uint32_t version;
  std::uint32_t actor;
  const char *id;
  PluginEventType type;
  float value[3];
};

struct PluginEvent {
  std::uint32_t actor = 0;
  EventRecord record;
};

[[nodiscard]] std::optional<PluginEvent>
ParsePluginEvent(const void *a_data, std::uint32_t a_length);
}
