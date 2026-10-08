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

std::string FloatBitsText(const std::optional<float> &a_value) {
  return a_value ? std::to_string(std::bit_cast<std::uint32_t>(*a_value))
                 : std::string{"none"};
}

std::string EventOriginIdentity(const EventInput &a_event,
                                const std::string &a_instance) {
  return Match(
      a_event.origin,
      [&](const EventOrigin &a_origin) -> std::string {
        return std::format("{}:event:{}:{}:{}:{}:{}:{}:{}:{}", a_instance,
                           a_origin.event.size(), a_origin.event,
                           a_origin.filter.node.size(), a_origin.filter.node,
                           a_origin.filter.arg.size(), a_origin.filter.arg,
                           FloatBitsText(a_origin.filter.value.min),
                           FloatBitsText(a_origin.filter.value.max));
      },
      [&](const PluginOrigin &a_origin) -> std::string {
        return a_instance + ":plugin:" + a_origin.id;
      });
}

Identity ExternalSourceIdentity(const ExternalSource &a_source,
                                const IdentityFacts &a_facts) {
  const std::string &instance = a_facts.instance;
  return Match(
      a_source, [&](const GeometryInput &) -> Identity { return a_facts.mesh; },
      [&](const MaterialInput &) -> Identity { return a_facts.material; },
      [&](const TextureInput &a_input) -> Identity {
        return ImageCacheKey(a_input.path);
      },
      [&](const NodePositionInput &a_input) -> Identity {
        return std::format("{}:{}:{}", a_facts.geometry, a_facts.root,
                           a_input.name);
      },
      [&](const ChartCountInput &) -> Identity {
        return "charts:" + a_facts.mesh;
      },
      [&](const ItemHashInput &) -> Identity {
        return std::format("item:{:08x}",
                           std::bit_cast<std::uint32_t>(a_facts.itemHash));
      },
      [&](const ActorValueSignal &a_input) -> Identity {
        return std::format("{}:{}:{}", instance,
                           static_cast<int>(a_input.measure),
                           a_input.actorValue);
      },
      [&](const ActorStateSignal &a_input) -> Identity {
        return std::format("{}:{}", instance, static_cast<int>(a_input.kind));
      },
      [&](const EnchantmentSignal &a_input) -> Identity {
        return EnchantmentIdentity(a_input, a_facts.enchantment);
      },
      [&](const EffectShaderInput &a_input) -> Identity {
        return "efsh:" + a_input.record.text;
      },
      [&](const EventInput &a_input) -> Identity {
        return EventOriginIdentity(a_input, instance);
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
  bindings.state = [a_facts](NodeId a_node) {
    return std::format("{}:{}", a_facts->instance, a_node);
  };
  bindings.external = [a_facts](const ExternalSource &a_source) -> Identity {
    return ExternalSourceIdentity(a_source, *a_facts);
  };
  return bindings;
}
}
