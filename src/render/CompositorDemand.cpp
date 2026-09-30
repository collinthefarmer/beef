// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"

#include "planners/TextureIdentity.h"
#include "render/MeshReader.h"
#include "render/SourceSampling.h"

#include <bit>
#include <format>
#include <memory>

namespace BetterEnchantmentEffects {
ValueBindings TextureValueBindings(const RecipeGraph &graph,
                                   const GeometryInputs &inputs) {
  const auto instance = std::make_shared<const std::string>(
      std::format("{}:{}", reinterpret_cast<std::uintptr_t>(&graph),
                  inputs.applicationContext));
  ValueBindings bindings;
  bindings.state = [instance](NodeId node) {
    return std::format("{}:{}", *instance, node);
  };
  bindings.external = [&inputs, instance](const ExternalSource &source)
      -> std::expected<std::string, std::string> {
    return Match(
        source,
        [&](const GeometryInput &) -> std::expected<std::string, std::string> {
          return std::format(
              "{}", reinterpret_cast<std::uintptr_t>(inputs.geometry.get()));
        },
        [&](const MaterialInput &) -> std::expected<std::string, std::string> {
          const auto &m = inputs.material;
          return std::format(
              "{}:{}:{}:{}:{}:{}:{}:{}:{}", TextureRefIdentity(m.diffuse),
              m.diffuse.Generation(), TextureRefIdentity(m.normal),
              m.normal.Generation(), TextureRefIdentity(m.rmaos),
              m.rmaos.Generation(), TextureRefIdentity(m.displacement),
              m.displacement.Generation(), m.flatDisplacement);
        },
        [&](const TextureInput &k) -> std::expected<std::string, std::string> {
          return ImageCacheKey(k.path);
        },
        [&](const NodePositionInput &k)
            -> std::expected<std::string, std::string> {
          return std::format(
              "{}:{}:{}",
              reinterpret_cast<std::uintptr_t>(inputs.geometry.get()),
              reinterpret_cast<std::uintptr_t>(inputs.root.get()), k.name);
        },
        [&](const ActorValueSignal &k)
            -> std::expected<std::string, std::string> {
          return std::format("{}:{}:{}", *instance, static_cast<int>(k.measure),
                             k.actorValue);
        },
        [&](const ActorStateSignal &k)
            -> std::expected<std::string, std::string> {
          return std::format("{}:{}", *instance, static_cast<int>(k.kind));
        },
        [&](const EnchantmentSignal &k)
            -> std::expected<std::string, std::string> {
          return std::format("{}:{}", *instance, static_cast<int>(k.field));
        },
        [&](const EffectShaderInput &k)
            -> std::expected<std::string, std::string> {
          return *instance + ":" + k.record.text;
        },
        [&](const EventInput &k) -> std::expected<std::string, std::string> {
          return Match(
              k.origin,
              [&](const EventOrigin &e)
                  -> std::expected<std::string, std::string> {
                const auto number = [](const std::optional<float> &v) {
                  return v ? std::to_string(std::bit_cast<std::uint32_t>(*v))
                           : std::string{"none"};
                };
                return std::format(
                    "{}:event:{}:{}:{}:{}:{}:{}:{}:{}", *instance,
                    e.event.size(), e.event, e.filter.node.size(),
                    e.filter.node, e.filter.arg.size(), e.filter.arg,
                    number(e.filter.value.min), number(e.filter.value.max));
              },
              [&](const PluginOrigin &e)
                  -> std::expected<std::string, std::string> {
                return *instance + ":plugin:" + e.id;
              });
        },
        [&](const RootTransformInput &)
            -> std::expected<std::string, std::string> { return *instance; },
        [&](const TimeInput &) -> std::expected<std::string, std::string> {
          return *instance;
        },
        [&](const DeltaTimeInput &) -> std::expected<std::string, std::string> {
          return *instance;
        },
        [](const SampleUvInput &) -> std::expected<std::string, std::string> {
          return "uv";
        });
  };
  return bindings;
}

}
