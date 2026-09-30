// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TextureDemand.h"

#include <algorithm>
#include <tuple>

namespace BetterEnchantmentEffects {
std::strong_ordering TextureKey::operator<=>(const TextureKey &other) const {
  if (const auto order = identity <=> other.identity; order != 0)
    return order;
  return std::tuple{requirements.size.Pixels(), requirements.format,
                    requirements.mipPolicy} <=>
         std::tuple{other.requirements.size.Pixels(), other.requirements.format,
                    other.requirements.mipPolicy};
}
namespace {
struct Collector {
  std::vector<TextureDemand> &demands;
  const ValueBindings &bindings;
  GeometryId geometry{};
  std::size_t visits = 0;

  std::expected<TextureDemandId, std::string>
  Add(TextureValue value, TextureRequirements requirements, std::size_t depth) {
    if (depth > 64 || ++visits > 4096)
      return std::unexpected("texture demand collection exceeds its limit");
    if (!value.graph)
      return std::unexpected("texture demand has no graph");
    const auto &graph = *value.graph;
    const auto *node = graph.NodeAt(value.output.node);
    if (!node || value.output.output != 0 ||
        graph.IsDisabled(value.output.node))
      return std::unexpected(
          "texture demand has an invalid or disabled output");
    const auto type = graph.OutputType(value.output);
    if (!type || !Is<ValueType>(*type))
      return std::unexpected(node->displayName +
                             ": output has no texture producer");
    auto identity = IdentifyValue(graph, value.output, bindings);
    if (!identity)
      return std::unexpected(identity.error());
    TextureKey key{std::move(*identity), requirements};
    for (std::size_t i = 0; i < demands.size(); ++i)
      if (demands[i].key == key)
        return i;
    if (demands.size() >= 4096)
      return std::unexpected("texture demand collection exceeds its limit");
    std::size_t identityBytes = key.identity.canonical.size();
    for (const auto &existing : demands)
      identityBytes += existing.key.identity.canonical.size();
    if (identityBytes > kMaxIdentityBytes)
      return std::unexpected(
          "texture identities exceed the collection size limit");
    TextureDemand demand{std::move(key), value, {}, {}, {}, geometry};
    if (Is<ExpressionOperation>(node->kind) &&
        graph.SampleDependent(value.output)) {
      auto program = FieldProgram::Compile(graph, value.output);
      if (!program)
        return std::unexpected(program.error());
      demand.program = std::move(*program);
    }
    const auto collect =
        [&](const auto &self, OutputRef ref, TextureRequirements wanted,
            std::size_t level) -> std::expected<void, std::string> {
      if (level > 64 || ++visits > 4096)
        return std::unexpected(
            "texture prerequisite traversal exceeds its limit");
      const auto *input = graph.NodeAt(ref.node);
      const auto inputType = graph.OutputType(ref);
      if (!input || !inputType)
        return std::unexpected("invalid texture prerequisite");
      if (!Is<ValueType>(*inputType))
        return {};
      if (graph.SampleDependent(ref) &&
          !Is<TextureCoordinatesOperation>(input->kind) &&
          !Is<ExternalInput>(input->kind)) {
        const auto dependency =
            Add({value.graph, ref, value.instance}, wanted, level + 1);
        if (!dependency)
          return std::unexpected(dependency.error());
        if (std::ranges::find(demand.dependencies, *dependency) ==
            demand.dependencies.end())
          demand.dependencies.push_back(*dependency);
        return {};
      }
      if (Is<ReductionOperation>(input->kind))
        wanted.format = TextureFormat::kRgba32Float;
      for (auto operand : InputsOf(input->kind))
        if (auto result = self(self, operand, wanted, level + 1); !result)
          return result;
      return {};
    };
    auto wanted = requirements;
    if (Is<ReductionOperation>(node->kind))
      wanted.format = TextureFormat::kRgba32Float;
    for (auto input : InputsOf(node->kind))
      if (auto result = collect(collect, input, wanted, depth + 1); !result)
        return std::unexpected(result.error());
    identityBytes = demand.key.identity.canonical.size();
    for (const auto &existing : demands)
      identityBytes += existing.key.identity.canonical.size();
    if (demands.size() >= 4096 || identityBytes > kMaxIdentityBytes)
      return std::unexpected(
          "texture demand collection exceeds its size limit");
    demands.push_back(std::move(demand));
    return demands.size() - 1;
  }
};
}
std::expected<TextureDemandId, std::string>
CollectTextureDemand(std::vector<TextureDemand> &demands, TextureValue value,
                     TextureRequirements requirements, const TextureUse &use,
                     const ValueBindings &bindings, GeometryId geometry) {
  const auto originalSize = demands.size();
  Collector collector{demands, bindings, geometry};
  auto result = collector.Add(value, requirements, 0);
  if (!result) {
    demands.erase(demands.begin() + originalSize, demands.end());
    return result;
  }
  std::vector<TextureDemandId> pending{*result};
  std::vector<bool> visited(demands.size(), false);
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (id >= demands.size() || visited[id])
      continue;
    visited[id] = true;
    auto &demand = demands[id];
    if (std::ranges::find(demand.dependents, use) == demand.dependents.end())
      demand.dependents.push_back(use);
    pending.insert(pending.end(), demand.dependencies.begin(),
                   demand.dependencies.end());
  }
  return result;
}
}
