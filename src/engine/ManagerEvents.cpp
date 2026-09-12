#include "engine/Manager.h"

#include <random>
#include <utility>

namespace BetterEnchantmentEffects {
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

void Manager::FireAt(RE::FormID a_actorID, std::string a_event,
                     std::string a_node, Vec3 a_offset, float a_random,
                     float a_value) {
  PostTask([this, a_actorID, event = std::move(a_event),
            node = std::move(a_node), a_offset, a_random, a_value] {
    EventRecord record;
    record.id = event;
    record.payload.value = a_value;
    record.payload.node = node;
    RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
    RE::NiAVObject *root = actor ? actor->Get3D(false) : nullptr;
    if (!node.empty() && root) {
      if (RE::NiAVObject *object =
              root->GetObjectByName(RE::BSFixedString{node})) {
        const RE::NiPoint3 &at = object->world.translate;
        Vec3 position{at.x + a_offset.x, at.y + a_offset.y, at.z + a_offset.z};
        if (a_random > 0.0f) {
          static std::mt19937 gen{std::random_device{}()};
          std::uniform_real_distribution<float> spread{-a_random, a_random};
          position.x += spread(gen);
          position.y += spread(gen);
          position.z += spread(gen);
        }
        record.payload.position = position;
      } else {
        logger::warn("fire {}: node '{}' is not on the actor", event, node);
      }
    }
    Fire(a_actorID, record);
  });
}

}
