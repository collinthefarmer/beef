// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "actors/Actor.h"
#include "engine/ApplicationService.h"
#include "engine/Environment.h"
#include "engine/JobQueue.h"
#include "planners/ActorMatches.h"
#include "planners/BindingPlan.h"
#include "planners/PlanSizes.h"
#include "planners/RenderBudget.h"
#include "planners/RenderPlan.h"
#include "planners/ResolutionLevel.h"
#include "planners/StackPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeCatalog.h"
#include "recipe/Signals.h"
#include "render/Binding.h"
#include "render/Compositor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
class RenderInstance;

struct PlacedOutput {
  std::size_t index = 0;
  std::unique_ptr<RenderOutput> stack;
  std::string problem;
  bool active = false;
  bool rendered = false;
  bool renderFailed = false;
};

struct MergedSlotStack {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<ChainLink> links;
  std::vector<std::size_t> layerStarts;
  std::size_t layerCount = 0;
  std::unique_ptr<RenderOutput> stack;
};

struct LiveGeometry {
  Actors::GeometryRef ref;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::BSLightingShaderProperty> property;
  std::string name;
  GeometryInputs inputs;
  std::unique_ptr<MaterialBinding> material;
  std::unique_ptr<ShellBinding> shell;
  std::optional<std::size_t> shellOwner;
  std::vector<PlacementId> placements;
  GeometryPlan plan;
  GeometryStackPlan stackPlan;
  std::vector<MergedSlotStack> mergedSlots;
  BindingPlan binding;
  bool lost = false;
};

enum class LivePieceId : std::size_t {};

struct LivePiece {
  Actors::PieceRef ref;
  Actors::ModelToken model{};
  RE::FormID armor = 0;
  std::optional<FormKey> addon;
  std::string armorName;
  RE::FormID enchantment = 0;
  std::vector<LiveGeometry> geometries;
};

struct LiveInstance {
  std::shared_ptr<const Recipe> recipe;
  RE::FormID enchantment = 0;
  std::string effectScope;
  std::shared_ptr<const RecipeGraph> graph;
  std::unique_ptr<SignalState> signals;
  std::unique_ptr<ActorEnvironment> environment;
  std::unique_ptr<LightBinding> light;
  std::optional<std::size_t> lightOutput;
  std::string lightProblem;
  std::uint32_t startMS = 0;
  float lastTime = 0.0f;
};

struct LivePlacement {
  GeometryId geometry{};
  std::vector<PlacedOutput> outputs;
};

struct ResizedStack {
  std::unique_ptr<RenderOutput> stack;
  bool drawn = false;
};

struct ResizeInFlight {
  ResolutionLevel to = ResolutionLevel::kFull;
  std::shared_ptr<RenderInstance> render;
  std::vector<ResizedStack> stacks;
};

struct HiddenSlot {
  std::size_t piece = 0;
  std::size_t geometry = 0;
  std::size_t slot = 0;
  bool drawn = false;
};

struct FirstDraw {
  std::vector<HiddenSlot> slots;
};

struct LiveActor {
  RE::ActorHandle actor;
  PlanSizes fullSizes;
  std::optional<ResizeInFlight> resize;
  std::optional<FirstDraw> firstDraw;
  std::optional<JobId> awaitedPlan;
  ResolutionLevel renderLevel = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  std::shared_ptr<const RecipeCatalog> catalog;
  ActorMatches matches;
  std::vector<LivePiece> pieces;
  std::vector<LiveInstance> instances;
  std::vector<LivePlacement> placements;
  std::vector<ApplicationToken> applications;
};

struct ResolvedPlacement {
  std::size_t placement = 0;
  std::size_t instance = 0;
};

void RetireGeometry(LiveGeometry &a_geometry);
void RetireGeometry(LiveActor &a_actor, LiveGeometry &a_geometry);
void RetireActorEffects(LiveActor &a_actor);
void SetAppliedResolution(LiveActor &a_actor, ResolutionLevel a_level);
[[nodiscard]] std::size_t LiveInstanceCount(const LiveActor &a_actor) noexcept;
[[nodiscard]] std::span<const Recipe>
RecipesOf(const LiveActor &a_actor) noexcept;
[[nodiscard]] SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface);
[[nodiscard]] PlacedOutput *OutputAt(LivePlacement &a_placement,
                                     std::size_t a_index);
[[nodiscard]] std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, PlacementId a_placement) noexcept;
[[nodiscard]] std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, const LiveGeometry &a_bound,
                 std::size_t a_placed) noexcept;
}
