// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/PluginEvents.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace BetterEnchantmentEffects {
namespace {
std::size_t ComponentsOf(PluginEventType a_type) noexcept {
  switch (a_type) {
  case PluginEventType::kScalar:
    return 1;
  case PluginEventType::kVec2:
    return 2;
  case PluginEventType::kVec3:
    return 3;
  }
  return 0;
}
}

std::optional<PluginEvent> ParsePluginEvent(const void *a_data,
                                            std::uint32_t a_length) {
  if (!a_data || a_length != sizeof(PluginEventMessage)) {
    return std::nullopt;
  }
  const auto *message = static_cast<const PluginEventMessage *>(a_data);
  if (message->version != kPluginEventVersion || !message->id) {
    return std::nullopt;
  }
  const std::size_t idLength = strnlen(message->id, kPluginEventIdMax + 1);
  if (idLength == 0 || idLength > kPluginEventIdMax) {
    return std::nullopt;
  }
  const std::size_t components = ComponentsOf(message->type);
  if (components == 0 ||
      !std::all_of(message->value, message->value + components,
                   [](float f) { return std::isfinite(f); })) {
    return std::nullopt;
  }
  PluginEvent event;
  event.actor = message->actor;
  event.record.plugin = true;
  event.record.id.assign(message->id, idLength);
  switch (message->type) {
  case PluginEventType::kScalar:
    event.record.payload.value = message->value[0];
    break;
  case PluginEventType::kVec2:
    event.record.payload.value = Vec2{message->value[0], message->value[1]};
    break;
  case PluginEventType::kVec3:
    event.record.payload.value =
        Vec3{message->value[0], message->value[1], message->value[2]};
    break;
  }
  return event;
}
}
