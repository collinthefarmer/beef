// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"

#include "planners/TextureIdentity.h"
#include "planners/ValueIdentity.h"
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

std::expected<std::string, std::string>
ExternalSourceIdentity(const ExternalSource &source,
                       const GeometryInputs &inputs,
                       const std::string &instance) {
  return Match(
      source,
      [&](const GeometryInput &) -> std::expected<std::string, std::string> {
        return MeshIdentityOf(inputs);
      },
      [&](const MaterialInput &) -> std::expected<std::string, std::string> {
        return MaterialIdentityOf(inputs.material);
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
      [&](const ChartCountInput &) -> std::expected<std::string, std::string> {
        return "charts:" + MeshIdentityOf(inputs);
      },
      [&](const ItemHashInput &) -> std::expected<std::string, std::string> {
        return std::format("item:{:08x}",
                           std::bit_cast<std::uint32_t>(inputs.itemHash));
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
        return EnchantmentIdentity(k, inputs.enchantment.fields);
      },
      [&](const EffectShaderInput &k)
          -> std::expected<std::string, std::string> {
        return "efsh:" + k.record.text;
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
      std::format("{}:{}:{:08X}", reinterpret_cast<std::uintptr_t>(&graph),
                  inputs.scope.context, inputs.scope.actor));
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

std::string MaterialIdentityOf(const MaterialInputs &material) {
  const auto counted = [](const TextureRef &a_texture) {
    const std::string identity = TextureIdentity(a_texture);
    return std::format("{}:{}", identity.size(), identity);
  };
  return std::format("{}|{}|{}|{}|{}", counted(material.diffuse),
                     counted(material.normal), counted(material.rmaos),
                     counted(material.displacement), material.flatDisplacement);
}

std::optional<std::string> MeshHashIdentityOf(const GeometryInputs &inputs) {
  RE::BSGeometry *geometry = inputs.geometry.get();
  Compositor *compositor = Compositor::GetSingleton();
  if (!geometry || !compositor) {
    return std::nullopt;
  }
  const auto entry = compositor->MeshOf(geometry);
  if (!entry || !*entry || !(*entry)->mesh) {
    return std::nullopt;
  }
  const MeshData &mesh = *(*entry)->mesh;
  return std::format("mesh:{:016x}:{}", mesh.hash, mesh.partitions.size());
}

std::string MeshIdentityOf(const GeometryInputs &inputs) {
  return MeshHashIdentityOf(inputs).value_or(std::format(
      "{}", reinterpret_cast<std::uintptr_t>(inputs.geometry.get())));
}
}
