// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/IdentityFacts.h"

#include "Core.h"
#include "planners/TextureIdentity.h"

#include <bit>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
using Identity = std::expected<std::string, std::string>;

std::string FloatBitsText(const std::optional<float> &value) {
  return value ? std::to_string(std::bit_cast<std::uint32_t>(*value))
               : std::string{"none"};
}

std::string EventOriginIdentity(const EventInput &event,
                                const std::string &instance) {
  return Match(
      event.origin,
      [&](const EventOrigin &origin) -> std::string {
        return std::format("{}:event:{}:{}:{}:{}:{}:{}:{}:{}", instance,
                           origin.event.size(), origin.event,
                           origin.filter.node.size(), origin.filter.node,
                           origin.filter.arg.size(), origin.filter.arg,
                           FloatBitsText(origin.filter.value.min),
                           FloatBitsText(origin.filter.value.max));
      },
      [&](const PluginOrigin &origin) -> std::string {
        return instance + ":plugin:" + origin.id;
      });
}

Identity ExternalSourceIdentity(const ExternalSource &source,
                                const IdentityFacts &facts) {
  const std::string &instance = facts.instance;
  return Match(
      source, [&](const GeometryInput &) -> Identity { return facts.mesh; },
      [&](const MaterialInput &) -> Identity { return facts.material; },
      [&](const TextureInput &k) -> Identity { return ImageCacheKey(k.path); },
      [&](const NodePositionInput &k) -> Identity {
        return std::format("{}:{}:{}", facts.geometry, facts.root, k.name);
      },
      [&](const ChartCountInput &) -> Identity {
        return "charts:" + facts.mesh;
      },
      [&](const ItemHashInput &) -> Identity {
        return std::format("item:{:08x}",
                           std::bit_cast<std::uint32_t>(facts.itemHash));
      },
      [&](const ActorValueSignal &k) -> Identity {
        return std::format("{}:{}:{}", instance, static_cast<int>(k.measure),
                           k.actorValue);
      },
      [&](const ActorStateSignal &k) -> Identity {
        return std::format("{}:{}", instance, static_cast<int>(k.kind));
      },
      [&](const EnchantmentSignal &k) -> Identity {
        return EnchantmentIdentity(k, facts.enchantment);
      },
      [&](const EffectShaderInput &k) -> Identity {
        return "efsh:" + k.record.text;
      },
      [&](const EventInput &k) -> Identity {
        return EventOriginIdentity(k, instance);
      },
      [&](const RootTransformInput &) -> Identity { return instance; },
      [&](const TimeInput &) -> Identity { return instance; },
      [&](const DeltaTimeInput &) -> Identity { return instance; },
      [](const SampleUvInput &) -> Identity { return "uv"; });
}
}

std::string InstanceIdentity(std::uintptr_t a_graph, std::size_t a_context,
                             std::uint32_t a_actor) {
  return std::format("{}:{}:{:08X}", a_graph, a_context, a_actor);
}

ValueBindings IdentityBindings(std::shared_ptr<const IdentityFacts> a_facts) {
  if (!a_facts) {
    a_facts = std::make_shared<const IdentityFacts>();
  }
  ValueBindings bindings;
  bindings.state = [a_facts](NodeId node) {
    return std::format("{}:{}", a_facts->instance, node);
  };
  bindings.external = [a_facts](const ExternalSource &source) -> Identity {
    return ExternalSourceIdentity(source, *a_facts);
  };
  return bindings;
}
}
