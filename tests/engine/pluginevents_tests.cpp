#include "engine/PluginEvents.h"
#include "test_support.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
PluginEventMessage Valid(const char *a_id) {
  PluginEventMessage message{};
  message.version = kPluginEventVersion;
  message.actor = 0x14;
  message.id = a_id;
  message.type = PluginEventType::kScalar;
  message.value[0] = 2.0f;
  return message;
}
}

int main() {
  {
    const PluginEventMessage message = Valid("myMod.charge");
    const auto event = ParsePluginEvent(&message, sizeof(message));
    Check(event.has_value(), "a valid scalar message parses");
    Check(event && event->actor == 0x14 && event->record.plugin &&
              event->record.id == "myMod.charge" &&
              Near(AsScalar(event->record.payload.value), 2.0f),
          "the parsed event carries actor, channel, id and value");
  }

  {
    PluginEventMessage message = Valid("myMod.impact");
    message.type = PluginEventType::kVec3;
    message.value[0] = 1.0f;
    message.value[1] = 2.0f;
    message.value[2] = 3.0f;
    const auto event = ParsePluginEvent(&message, sizeof(message));
    const auto *carried =
        event ? Get<Vec3>(event->record.payload.value) : nullptr;
    Check(carried && Near(carried->x, 1.0f) && Near(carried->z, 3.0f),
          "a vec3 message parses to a vec3 value");
  }

  Check(!ParsePluginEvent(nullptr, sizeof(PluginEventMessage)),
        "null data is rejected");

  {
    const PluginEventMessage message = Valid("myMod.charge");
    Check(!ParsePluginEvent(&message, sizeof(message) - 1),
          "a short message is rejected");
    Check(!ParsePluginEvent(&message, sizeof(message) + 1),
          "a long message is rejected");
  }

  {
    PluginEventMessage message = Valid("myMod.charge");
    message.version = 2;
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "an unknown version is rejected");
  }

  {
    PluginEventMessage message = Valid(nullptr);
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "a null id is rejected");
    message.id = "";
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "an empty id is rejected");
  }

  {
    const std::string longId(kPluginEventIdMax + 1, 'x');
    const PluginEventMessage message = Valid(longId.c_str());
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "an over-long id is rejected without reading past the bound");
    const std::string maxId(kPluginEventIdMax, 'x');
    const PluginEventMessage atMax = Valid(maxId.c_str());
    Check(ParsePluginEvent(&atMax, sizeof(atMax)).has_value(),
          "an id at the bound parses");
  }

  {
    PluginEventMessage message = Valid("myMod.charge");
    message.type = static_cast<PluginEventType>(7);
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "an unknown type tag is rejected");
  }

  {
    PluginEventMessage message = Valid("myMod.charge");
    message.value[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "a NaN component is rejected");
    message.type = PluginEventType::kVec2;
    message.value[0] = 1.0f;
    message.value[1] = std::numeric_limits<float>::infinity();
    Check(!ParsePluginEvent(&message, sizeof(message)),
          "an infinite component is rejected");
  }

  {
    std::array<std::uint8_t, sizeof(PluginEventMessage)> garbage{};
    garbage.fill(0xA5);
    Check(!ParsePluginEvent(garbage.data(), garbage.size()),
          "uniform garbage bytes are rejected");
  }

  return test::Finish("engine pluginevents");
}
