// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include <optional>
#include <random>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
RE::NiAVObject *ThirdPersonRoot(RE::FormID a_actor) {
  RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_actor);
  return actor ? actor->Get3D(false) : nullptr;
}

std::optional<Vec3> NodeWorldPosition(RE::FormID a_actor,
                                      const std::string &a_node) {
  RE::NiAVObject *root = ThirdPersonRoot(a_actor);
  if (!root) {
    return std::nullopt;
  }
  RE::NiAVObject *object = root->GetObjectByName(RE::BSFixedString{a_node});
  if (!object) {
    return std::nullopt;
  }
  const RE::NiPoint3 &at = object->world.translate;
  return Vec3{at.x, at.y, at.z};
}

Vec3 Scattered(Vec3 a_position, float a_random) {
  if (a_random <= 0.0f) {
    return a_position;
  }
  static std::mt19937 gen{std::random_device{}()};
  std::uniform_real_distribution<float> spread{-a_random, a_random};
  a_position.x += spread(gen);
  a_position.y += spread(gen);
  a_position.z += spread(gen);
  return a_position;
}

EventRecord TriggerEventOf(const Studio::FireTrigger &a_trigger) {
  EventRecord record;
  record.id = a_trigger.event;
  record.payload.value = a_trigger.value;
  record.payload.node = a_trigger.node;
  return record;
}

EventRecord PositionEventOf(const Studio::FireTrigger &a_trigger,
                            Vec3 a_position) {
  EventRecord record;
  record.id = a_trigger.event + ".position";
  record.payload.value = a_position;
  record.payload.node = a_trigger.node;
  return record;
}
}

void Manager::Fire(RE::FormID a_actorID, const EventRecord &a_event) {
  const auto it = applied_.find(a_actorID);
  if (it == applied_.end()) {
    return;
  }
  for (LiveInstance &instance : it->second.instances) {
    if (instance.signals) {
      instance.signals->Fire(a_event, instance.lastTime);
    }
  }
}

void Manager::QueueEvent(RE::FormID a_actorID, EventRecord a_event) {
  if (a_actorID == 0) {
    return;
  }
  PostTask([this, a_actorID, event = std::move(a_event)] {
    Fire(a_actorID, event);
  });
}

void Manager::QueueBroadcast(EventRecord a_event) {
  PostTask([this, event = std::move(a_event)] {
    for (const auto &entry : applied_) {
      Fire(entry.first, event);
    }
  });
}

void Manager::FireAt(Studio::FireTrigger a_trigger) {
  PostTask([this, trigger = std::move(a_trigger)] {
    Fire(trigger.actorID, TriggerEventOf(trigger));
    if (trigger.node.empty()) {
      return;
    }
    if (!ThirdPersonRoot(trigger.actorID)) {
      return;
    }
    const std::optional<Vec3> node =
        NodeWorldPosition(trigger.actorID, trigger.node);
    if (!node) {
      logger::warn("fire {}: node '{}' is not on the actor", trigger.event,
                   trigger.node);
      return;
    }
    const Vec3 offset = trigger.offset;
    const Vec3 position = Scattered(
        Vec3{node->x + offset.x, node->y + offset.y, node->z + offset.z},
        trigger.random);
    Fire(trigger.actorID, PositionEventOf(trigger, position));
  });
}

}
