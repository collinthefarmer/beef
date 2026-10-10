// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/ActorMatches.h"
#include "planners/ActorPlanning.h"
#include "planners/BindingPlan.h"
#include "planners/OutputState.h"
#include "planners/PlanSizes.h"
#include "planners/RenderBudget.h"
#include "planners/RenderPlan.h"
#include "planners/ResolutionLevel.h"
#include "planners/StackPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct PlacedOutputState {
  std::size_t output = 0;
  OutputState state;
  [[nodiscard]] bool operator==(const PlacedOutputState &) const = default;
};

struct PlacementOutputs {
  PlacementId placement{};
  GeometryId geometry{};
  std::vector<PlacedOutputState> outputs;
  [[nodiscard]] bool operator==(const PlacementOutputs &) const = default;
};

struct GeometryBinding {
  GeometryId geometry{};
  GeometryPlacementPlan placement;
  GeometryStackPlan stacks;
  BindingPlan binding;
  std::optional<InstanceId> shellOwner;
};

struct SurfacePlan {
  std::vector<GeometryBinding> geometries;
  std::vector<PlacementOutputs> placements;
};

struct SlotTargetState {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::string problem;
  [[nodiscard]] bool operator==(const SlotTargetState &) const = default;
};

struct BoundSurfaces {
  GeometryId geometry{};
  bool material = false;
  bool shell = false;
  std::vector<SlotTargetState> targets;
};

struct StackRequest {
  GeometryId geometry{};
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  PlacementId placement{};
  InstanceId instance{};
  std::size_t output = 0;
  Resolution resolution = Resolution::kFull;
  [[nodiscard]] bool operator==(const StackRequest &) const = default;
};

struct OutputActivation {
  std::vector<PlacementOutputs> placements;
  std::vector<StackRequest> stacks;
  std::vector<SlotChainRequest> chains;
};

struct SidedStack {
  StackRequest request;
  SideRef side;
  [[nodiscard]] bool operator==(const SidedStack &) const = default;
};

struct RenderBuildRequest {
  std::vector<SidedStack> stacks;
  std::vector<SlotChainRequest> chains;
  PlanSizes full;
};

struct RenderBuild {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::uint64_t chargedBytes = 0;
  PlanSizes sizes;
  PerLevel<std::optional<LevelCost>> levelCosts;
};

struct StackAttachment {
  std::size_t stack = 0;
  StepOutputRef result;
  [[nodiscard]] bool operator==(const StackAttachment &) const = default;
};

struct MergedAttachment {
  GeometryId geometry{};
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<ChainLink> links;
  std::vector<std::size_t> layerStarts;
  std::size_t layerCount = 0;
  StepOutputRef result;
  SideRef side;
  bool animated = false;
};

struct Attachments {
  std::vector<StackAttachment> stacks;
  std::vector<MergedAttachment> merged;
  std::vector<PlacementOutputs> placements;
};

struct LightRequest {
  InstanceId instance{};
  std::size_t output = 0;
  std::vector<GeometryId> geometries;
};

struct InstanceLightProblem {
  InstanceId instance{};
  std::string problem;
  [[nodiscard]] bool operator==(const InstanceLightProblem &) const = default;
};

struct LightProgram {
  std::vector<LightRequest> lights;
  std::vector<InstanceLightProblem> problems;
};

inline constexpr std::string_view kShellNotCreated =
    "shell could not be created";
inline constexpr std::string_view kMaterialNotBound = "material binding failed";
inline constexpr std::string_view kSurfaceNotBound = "the surface is not bound";
inline constexpr std::string_view kStackNotLowered = "stack was not lowered";
inline constexpr std::string_view kNoEligibleLight = "no eligible light output";

[[nodiscard]] SurfacePlan PlanSurfaces(const ActorMatches &a_matches,
                                       std::span<const Recipe> a_store,
                                       const OutputFilter &a_filter = {});
[[nodiscard]] OutputActivation
ActivateOutputs(const SurfacePlan &a_plan,
                std::span<const BoundSurfaces> a_bound,
                const ActorMatches &a_matches, std::span<const Recipe> a_store,
                std::span<const InstanceId> a_compiled);
[[nodiscard]] std::expected<RenderBuildRequest, std::string>
RenderBuildRequestOf(const OutputActivation &a_activation,
                     std::span<const TextureSize> a_sizes);
[[nodiscard]] PerLevel<std::optional<LevelCost>>
LevelCostsOf(const RenderPlan &a_plan, const PlanSizes &a_full);
[[nodiscard]] std::expected<RenderBuild, std::string>
RenderBuildAt(const RenderPlan &a_plan, const PlanSizes &a_full,
              const InstallBound &a_bound);
[[nodiscard]] Attachments
AttachmentsOf(const RenderPlan &a_plan, const RenderBuildRequest &a_request,
              std::span<const GeometryBinding> a_bound,
              std::vector<PlacementOutputs> a_outputs);
[[nodiscard]] std::vector<PlacementOutputs>
FailedOutputs(std::vector<PlacementOutputs> a_outputs,
              std::span<const StackRequest> a_stacks,
              const std::string &a_problem);
[[nodiscard]] LightProgram PlanLightsOf(const ActorMatches &a_matches,
                                        std::span<const Recipe> a_store,
                                        const OutputFilter &a_filter = {});
}
