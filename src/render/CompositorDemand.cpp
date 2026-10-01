// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"

#include "planners/TextureIdentity.h"
#include "render/MeshReader.h"
#include "render/SourceSampling.h"

#include <bit>
#include <format>
#include <memory>

namespace BetterEnchantmentEffects {
namespace {
std::string FloatBitsText(const std::optional<float> &value) {
  return value ? std::to_string(std::bit_cast<std::uint32_t>(*value))
               : std::string{"none"};
}

std::string MaterialInputsIdentity(const MaterialInputs &material) {
  return std::format(
      "{}:{}:{}:{}:{}:{}:{}:{}:{}", TextureRefIdentity(material.diffuse),
      material.diffuse.Generation(), TextureRefIdentity(material.normal),
      material.normal.Generation(), TextureRefIdentity(material.rmaos),
      material.rmaos.Generation(), TextureRefIdentity(material.displacement),
      material.displacement.Generation(), material.flatDisplacement);
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

std::string GeometryIdentity(const GeometryInputs &inputs) {
  RE::BSGeometry *geometry = inputs.geometry.get();
  Compositor *compositor = Compositor::GetSingleton();
  if (geometry && compositor) {
    const auto entry = compositor->MeshOf(geometry);
    if (entry && *entry && (*entry)->mesh) {
      const MeshData &mesh = *(*entry)->mesh;
      return std::format("mesh:{:016x}:{}", mesh.hash, mesh.partitions.size());
    }
  }
  return std::format("{}", reinterpret_cast<std::uintptr_t>(geometry));
}

std::expected<std::string, std::string>
ExternalSourceIdentity(const ExternalSource &source,
                       const GeometryInputs &inputs,
                       const std::string &instance) {
  return Match(
      source,
      [&](const GeometryInput &) -> std::expected<std::string, std::string> {
        return GeometryIdentity(inputs);
      },
      [&](const MaterialInput &) -> std::expected<std::string, std::string> {
        return MaterialInputsIdentity(inputs.material);
      },
      [&](const TextureInput &k) -> std::expected<std::string, std::string> {
        return ImageCacheKey(k.path);
      },
      [&](const NodePositionInput &k)
          -> std::expected<std::string, std::string> {
        return std::format(
            "{}:{}:{}", reinterpret_cast<std::uintptr_t>(inputs.geometry.get()),
            reinterpret_cast<std::uintptr_t>(inputs.root.get()), k.name);
      },
      [&](const ActorValueSignal &k)
          -> std::expected<std::string, std::string> {
        return std::format("{}:{}:{}", instance, static_cast<int>(k.measure),
                           k.actorValue);
      },
      [&](const ActorStateSignal &k)
          -> std::expected<std::string, std::string> {
        return std::format("{}:{}", instance, static_cast<int>(k.kind));
      },
      [&](const EnchantmentSignal &k)
          -> std::expected<std::string, std::string> {
        return std::format("{}:{}", instance, static_cast<int>(k.field));
      },
      [&](const EffectShaderInput &k)
          -> std::expected<std::string, std::string> {
        return instance + ":" + k.record.text;
      },
      [&](const EventInput &k) -> std::expected<std::string, std::string> {
        return EventOriginIdentity(k, instance);
      },
      [&](const RootTransformInput &)
          -> std::expected<std::string, std::string> { return instance; },
      [&](const TimeInput &) -> std::expected<std::string, std::string> {
        return instance;
      },
      [&](const DeltaTimeInput &) -> std::expected<std::string, std::string> {
        return instance;
      },
      [](const SampleUvInput &) -> std::expected<std::string, std::string> {
        return "uv";
      });
}
}

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
    return ExternalSourceIdentity(source, inputs, *instance);
  };
  return bindings;
}

}
