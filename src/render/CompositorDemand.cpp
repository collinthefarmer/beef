// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"

#include "planners/IdentityFacts.h"
#include "planners/TextureIdentity.h"
#include "planners/ValueIdentity.h"
#include "render/MeshReader.h"
#include "render/SourceSampling.h"

#include <format>
#include <memory>

namespace BetterEnchantmentEffects {
std::optional<float> ChartCountOf(const GeometryInputs &inputs) {
  auto *compositor = Compositor::GetSingleton();
  const auto mesh =
      compositor ? compositor->MeshOf(inputs.geometry.get(), MeshUse::kSummary)
                 : std::unexpected(std::string{"no compositor"});
  if (!mesh || !*mesh) {
    return std::nullopt;
  }
  return static_cast<float>((*mesh)->analysis.charts);
}

ItemFacts ItemFactsOf(const GeometryInputs &inputs) {
  return ItemFacts{inputs.itemHash, ChartCountOf(inputs).value_or(0.0f),
                   static_cast<float>(inputs.geometryIndex),
                   static_cast<float>(inputs.geometryCount)};
}

IdentityFacts IdentityFactsOf(const RecipeGraph &graph,
                              const GeometryInputs &inputs) {
  return {InstanceIdentity(reinterpret_cast<std::uintptr_t>(&graph),
                           inputs.scope.context, inputs.scope.actor),
          MeshIdentityOf(inputs),
          MaterialIdentityOf(inputs.material),
          reinterpret_cast<std::uintptr_t>(inputs.geometry.get()),
          reinterpret_cast<std::uintptr_t>(inputs.root.get()),
          inputs.itemHash,
          inputs.enchantment.fields,
          inputs.shader,
          inputs.name,
          inputs.geometryIndex,
          inputs.geometryCount};
}

ValueBindings TextureValueBindings(const RecipeGraph &graph,
                                   const GeometryInputs &inputs) {
  return IdentityBindings(
      std::make_shared<const IdentityFacts>(IdentityFactsOf(graph, inputs)));
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
  const auto entry = compositor->MeshOf(geometry, MeshUse::kSummary);
  if (!entry || !*entry) {
    return std::nullopt;
  }
  return std::format("mesh:{:016x}:{}", (*entry)->hash, (*entry)->partitions);
}

std::string MeshIdentityOf(const GeometryInputs &inputs) {
  return MeshHashIdentityOf(inputs).value_or(std::format(
      "{}", reinterpret_cast<std::uintptr_t>(inputs.geometry.get())));
}
}
