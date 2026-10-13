// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlanKey.h"

#include <algorithm>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
void Mix(std::size_t &a_hash, std::size_t a_value) noexcept {
  a_hash ^= a_value + 0x9e3779b97f4a7c15ULL + (a_hash << 6) + (a_hash >> 2);
}

void MixText(std::size_t &a_hash, std::string_view a_text) noexcept {
  Mix(a_hash, std::hash<std::string_view>{}(a_text));
}

std::optional<std::vector<std::string>>
StaticInputIdentities(const RecipeGraph &a_graph,
                      const ValueBindings &a_bindings) {
  std::vector<std::string> identities;
  for (const ExternalSource &source : StaticExternalSources(a_graph)) {
    if (!a_bindings.external) {
      return std::nullopt;
    }
    std::expected<std::string, std::string> identity =
        a_bindings.external(source);
    if (!identity) {
      return std::nullopt;
    }
    identities.push_back(std::move(*identity));
  }
  for (const GeometryBake &bake : GeometryBakesOf(a_graph)) {
    identities.push_back(std::format("geometry {}:{}", bake.geometry,
                                     IsNamedBy(bake, a_bindings.geometry)));
  }
  return identities;
}

std::optional<StackKey> StackKeyOf(const StackKeyRequest &a_request) {
  if (!a_request.graph || !a_request.meshHash ||
      ReadsNodePositions(*a_request.graph)) {
    return std::nullopt;
  }
  std::optional<std::vector<std::string>> inputs =
      StaticInputIdentities(*a_request.graph, a_request.bindings);
  if (!inputs) {
    return std::nullopt;
  }
  return StackKey{a_request.graph,    a_request.output,    a_request.placement,
                  a_request.geometry, a_request.context,   a_request.side,
                  a_request.material, *a_request.meshHash, std::move(*inputs)};
}
}

std::size_t
RenderPlanKeyHash::operator()(const RenderPlanKey &a_key) const noexcept {
  std::size_t hash = 0;
  for (const StackKey &stack : a_key.stacks) {
    Mix(hash, std::hash<const RecipeGraph *>{}(stack.graph));
    Mix(hash, stack.output);
    Mix(hash, IndexOf(stack.placement));
    Mix(hash, IndexOf(stack.geometry));
    Mix(hash, stack.context);
    Mix(hash, stack.side.index);
    MixText(hash, stack.material);
    MixText(hash, stack.meshHash);
    for (const std::string &input : stack.staticInputs) {
      MixText(hash, input);
    }
  }
  for (const ChainKey &chain : a_key.chains) {
    Mix(hash, IndexOf(chain.geometry));
    Mix(hash, static_cast<std::size_t>(chain.surface));
    Mix(hash, static_cast<std::size_t>(chain.slot));
    for (const ChainLink &link : chain.links) {
      Mix(hash, IndexOf(link.placement));
      Mix(hash, link.output);
    }
  }
  return hash;
}

std::optional<RenderPlanKey>
RenderPlanKeyOf(std::span<const StackKeyRequest> a_stacks,
                std::span<const SlotChainRequest> a_chains) {
  RenderPlanKey key;
  for (const StackKeyRequest &request : a_stacks) {
    std::optional<StackKey> stack = StackKeyOf(request);
    if (!stack) {
      return std::nullopt;
    }
    key.stacks.push_back(std::move(*stack));
  }
  for (const SlotChainRequest &chain : a_chains) {
    key.chains.push_back(
        ChainKey{chain.geometry, chain.surface, chain.slot, chain.links});
  }
  return key;
}

std::vector<std::shared_ptr<const RecipeGraph>>
GraphsNamedBy(const RenderPlanKey &a_key,
              std::span<const std::shared_ptr<const RecipeGraph>> a_graphs) {
  std::vector<std::shared_ptr<const RecipeGraph>> named;
  for (const std::shared_ptr<const RecipeGraph> &graph : a_graphs) {
    const bool requested =
        std::ranges::any_of(a_key.stacks, [&](const StackKey &a_stack) {
          return a_stack.graph == graph.get();
        });
    if (requested && !std::ranges::contains(named, graph)) {
      named.push_back(graph);
    }
  }
  return named;
}
}
