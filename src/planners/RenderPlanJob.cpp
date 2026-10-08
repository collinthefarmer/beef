// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlanJob.h"

#include <chrono>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
using SteadyClock = std::chrono::steady_clock;

std::uint64_t MicrosSince(SteadyClock::time_point a_start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() -
                                                            a_start)
          .count());
}

struct LayerRole {
  std::size_t layer = 0;
  TextureUseInput role = TextureUseInput::kSource;
};

std::string LayerProperty(std::size_t a_output, LayerRole a_role) {
  return LayerWhere(a_output, a_role.layer) +
         (a_role.role == TextureUseInput::kSource ? " source" : " mask");
}

void CollectLayerRoleDemands(std::vector<TextureDemand> &a_demands,
                             const PlanStackRequest &a_request,
                             LayerRole a_role,
                             std::vector<std::string> &a_warnings) {
  const RecipeGraph &graph = *a_request.graph;
  const std::string property = LayerProperty(a_request.outputIndex, a_role);
  for (const OutputBinding &binding : graph.OutputBindings()) {
    if (binding.property != property || !graph.SampleDependent(binding.value)) {
      continue;
    }
    const std::expected<TextureDemandId, std::string> collected =
        CollectTextureDemand(a_demands,
                             {{&graph, binding.value, a_request.context},
                              {a_request.side},
                              {a_request.placement, a_request.outputIndex,
                               a_role.layer, a_role.role},
                              a_request.geometry},
                             IdentityBindings(a_request.identity));
    if (!collected) {
      a_warnings.push_back(std::format("recipe {} {}: {}", a_request.recipe,
                                       property, collected.error()));
    }
  }
}

std::vector<TextureDemand>
CollectLayerDemands(const PlanBuildInput &a_input,
                    std::vector<std::string> &a_warnings) {
  std::vector<TextureDemand> demands;
  for (const PlanStackRequest &request : a_input.requests) {
    if (!request.graph || !request.output) {
      continue;
    }
    for (std::size_t layer = 0; layer < request.output->stack.size(); ++layer) {
      for (const TextureUseInput role :
           {TextureUseInput::kSource, TextureUseInput::kMask}) {
        CollectLayerRoleDemands(demands, request, {layer, role}, a_warnings);
      }
    }
  }
  return demands;
}

std::vector<RenderStackRequest> StackRequestsOf(const PlanBuildInput &a_input) {
  std::vector<RenderStackRequest> stacks;
  stacks.reserve(a_input.requests.size());
  for (const PlanStackRequest &request : a_input.requests) {
    stacks.push_back({request.graph.get(),
                      request.output.get(),
                      request.context,
                      request.placement,
                      request.outputIndex,
                      {request.side},
                      request.geometry});
  }
  return stacks;
}

RenderBindingResolver ResolverOf(const PlanBuildInput &a_input) {
  return [&a_input](const TextureValue &a_value,
                    GeometryId a_geometry) -> ValueBindings {
    for (const PlanStackRequest &request : a_input.requests) {
      if (request.graph.get() == a_value.graph &&
          request.context == a_value.instance &&
          request.geometry == a_geometry) {
        return IdentityBindings(request.identity);
      }
    }
    return ValueBindings{};
  };
}
}

PlanBuildOutput BuildPlanFrom(const PlanBuildInput &a_input) {
  PlanBuildOutput output{
      std::unexpected(std::string{"no plan was built"}), {}, {}};
  const SteadyClock::time_point demandStart = SteadyClock::now();
  const std::vector<TextureDemand> demands =
      CollectLayerDemands(a_input, output.warnings);
  output.phases.demandUs = MicrosSince(demandStart);
  output.phases.demands = demands.size();
  const SteadyClock::time_point lowerStart = SteadyClock::now();
  output.plan = BuildRenderPlan(demands, StackRequestsOf(a_input),
                                ResolverOf(a_input), a_input.chains);
  output.phases.lowerUs = MicrosSince(lowerStart);
  return output;
}
}
