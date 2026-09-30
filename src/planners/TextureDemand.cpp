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
std::expected<const RecipeNode *, std::string>
ProducerOf(const TextureValue &value) {
  if (!value.graph)
    return std::unexpected("texture demand has no graph");
  const RecipeGraph &graph = *value.graph;
  const RecipeNode *node = graph.NodeAt(value.output.node);
  if (!node || value.output.output != 0 || graph.IsDisabled(value.output.node))
    return std::unexpected("texture demand has an invalid or disabled output");
  const std::optional<GraphValueType> type = graph.OutputType(value.output);
  if (!type || !Is<ValueType>(*type))
    return std::unexpected(node->displayName +
                           ": output has no texture producer");
  return node;
}
std::optional<TextureDemandId>
FindDemand(std::span<const TextureDemand> demands, const TextureKey &key) {
  for (std::size_t i = 0; i < demands.size(); ++i)
    if (demands[i].key == key)
      return i;
  return std::nullopt;
}
bool WithinCollectionLimits(std::span<const TextureDemand> demands,
                            const TextureKey &added) {
  if (demands.size() >= 4096)
    return false;
  std::size_t identityBytes = added.identity.canonical.size();
  for (const TextureDemand &existing : demands)
    identityBytes += existing.key.identity.canonical.size();
  return identityBytes <= kMaxIdentityBytes;
}
std::expected<std::optional<FieldProgram>, std::string>
FieldProgramFor(const RecipeGraph &graph, const RecipeNode &node,
                OutputRef output) {
  if (!Is<ExpressionOperation>(node.kind) || !graph.SampleDependent(output))
    return std::optional<FieldProgram>{};
  std::expected<FieldProgram, std::string> program =
      FieldProgram::Compile(graph, output);
  if (!program)
    return std::unexpected(program.error());
  return std::optional<FieldProgram>{std::move(*program)};
}
TextureRequirements RequirementsForOperands(const NodeKind &kind,
                                            TextureRequirements requirements) {
  if (Is<ReductionOperation>(kind))
    requirements.format = TextureFormat::kRgba32Float;
  return requirements;
}
bool NeedsOwnDemand(const RecipeGraph &graph, const RecipeNode &node,
                    OutputRef output) {
  return graph.SampleDependent(output) &&
         !Is<TextureCoordinatesOperation>(node.kind) &&
         !Is<ExternalInput>(node.kind);
}
struct Collector {
  std::vector<TextureDemand> &demands;
  const ValueBindings &bindings;
  GeometryId geometry{};
  std::size_t visits = 0;

  std::expected<TextureDemandId, std::string>
  Add(TextureValue value, TextureRequirements requirements, std::size_t depth) {
    if (depth > 64 || ++visits > 4096)
      return std::unexpected("texture demand collection exceeds its limit");
    const std::expected<const RecipeNode *, std::string> producer =
        ProducerOf(value);
    if (!producer)
      return std::unexpected(producer.error());
    const RecipeNode &node = **producer;
    std::expected<TextureKey, std::string> key = KeyFor(value, requirements);
    if (!key)
      return std::unexpected(key.error());
    if (const std::optional<TextureDemandId> existing =
            FindDemand(demands, *key))
      return *existing;
    if (demands.size() >= 4096)
      return std::unexpected("texture demand collection exceeds its limit");
    if (!WithinCollectionLimits(demands, *key))
      return std::unexpected(
          "texture identities exceed the collection size limit");
    std::expected<std::optional<FieldProgram>, std::string> program =
        FieldProgramFor(*value.graph, node, value.output);
    if (!program)
      return std::unexpected(program.error());
    TextureDemand demand{std::move(*key),     value,   {}, {},
                         std::move(*program), geometry};
    const TextureRequirements wanted =
        RequirementsForOperands(node.kind, requirements);
    for (const OutputRef operand : InputsOf(node.kind))
      if (const std::expected<void, std::string> collected =
              CollectPrerequisites(demand, operand, wanted, depth + 1);
          !collected)
        return std::unexpected(collected.error());
    if (!WithinCollectionLimits(demands, demand.key))
      return std::unexpected(
          "texture demand collection exceeds its size limit");
    demands.push_back(std::move(demand));
    return demands.size() - 1;
  }

  std::expected<TextureKey, std::string>
  KeyFor(const TextureValue &value, TextureRequirements requirements) const {
    std::expected<ValueIdentity, std::string> identity =
        IdentifyValue(*value.graph, value.output, bindings);
    if (!identity)
      return std::unexpected(identity.error());
    return TextureKey{std::move(*identity), requirements};
  }

  std::expected<void, std::string>
  CollectPrerequisites(TextureDemand &demand, OutputRef operand,
                       TextureRequirements wanted, std::size_t depth) {
    if (depth > 64 || ++visits > 4096)
      return std::unexpected(
          "texture prerequisite traversal exceeds its limit");
    if (!demand.value.graph)
      return std::unexpected("invalid texture prerequisite");
    const RecipeGraph &graph = *demand.value.graph;
    const RecipeNode *input = graph.NodeAt(operand.node);
    const std::optional<GraphValueType> inputType = graph.OutputType(operand);
    if (!input || !inputType)
      return std::unexpected("invalid texture prerequisite");
    if (!Is<ValueType>(*inputType))
      return {};
    if (NeedsOwnDemand(graph, *input, operand))
      return AddDependency(demand, operand, wanted, depth);
    const TextureRequirements forOperands =
        RequirementsForOperands(input->kind, wanted);
    for (const OutputRef next : InputsOf(input->kind))
      if (std::expected<void, std::string> collected =
              CollectPrerequisites(demand, next, forOperands, depth + 1);
          !collected)
        return collected;
    return {};
  }

  std::expected<void, std::string> AddDependency(TextureDemand &demand,
                                                 OutputRef operand,
                                                 TextureRequirements wanted,
                                                 std::size_t depth) {
    const std::expected<TextureDemandId, std::string> dependency =
        Add({demand.value.graph, operand, demand.value.instance}, wanted,
            depth + 1);
    if (!dependency)
      return std::unexpected(dependency.error());
    if (std::ranges::find(demand.dependencies, *dependency) ==
        demand.dependencies.end())
      demand.dependencies.push_back(*dependency);
    return {};
  }
};
void RecordUse(std::vector<TextureDemand> &demands, TextureDemandId root,
               const TextureUse &use) {
  std::vector<TextureDemandId> pending{root};
  std::vector<bool> visited(demands.size(), false);
  while (!pending.empty()) {
    const TextureDemandId id = pending.back();
    pending.pop_back();
    if (id >= demands.size() || visited[id])
      continue;
    visited[id] = true;
    TextureDemand &demand = demands[id];
    if (std::ranges::find(demand.dependents, use) == demand.dependents.end())
      demand.dependents.push_back(use);
    pending.insert(pending.end(), demand.dependencies.begin(),
                   demand.dependencies.end());
  }
}
}
std::expected<TextureDemandId, std::string>
CollectTextureDemand(std::vector<TextureDemand> &demands,
                     const TextureDemandRequest &request,
                     const ValueBindings &bindings) {
  const std::size_t originalSize = demands.size();
  Collector collector{demands, bindings, request.geometry};
  std::expected<TextureDemandId, std::string> result =
      collector.Add(request.value, request.requirements, 0);
  if (!result) {
    demands.erase(demands.begin() + static_cast<std::ptrdiff_t>(originalSize),
                  demands.end());
    return result;
  }
  RecordUse(demands, *result, request.use);
  return result;
}
}
