// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "engine/ApplicationService.h"
#include "engine/Environment.h"
#include "planners/ActorMatches.h"
#include "planners/BindingPlan.h"
#include "planners/RenderPlan.h"
#include "planners/StackPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "render/Binding.h"
#include "render/Compositor.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
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
  RE::FormID armor = 0;
  std::optional<FormKey> addon;
  std::string armorName;
  RE::FormID enchantment = 0;
  std::vector<LiveGeometry> geometries;
};

struct LiveInstance {
  const Recipe *recipe = nullptr;
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

struct WornEntry {
  RE::FormID item = 0;
  RE::FormID addon = 0;
  RE::FormID enchantment = 0;
  RE::NiPointer<RE::NiAVObject> clone;
  bool firstPerson = false;
  bool inventoryWorn = false;
  [[nodiscard]] bool operator==(const WornEntry &a_other) const noexcept {
    return item == a_other.item && addon == a_other.addon &&
           enchantment == a_other.enchantment &&
           inventoryWorn == a_other.inventoryWorn &&
           clone.get() == a_other.clone.get() &&
           firstPerson == a_other.firstPerson;
  }
};

struct LiveActor {
  RE::ActorHandle actor;
  std::vector<WornEntry> worn;
  bool firstRenderAdmitted = false;
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
[[nodiscard]] std::size_t LiveInstanceCount(const LiveActor &a_actor) noexcept;
[[nodiscard]] SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface);
[[nodiscard]] PlacedOutput *OutputAt(LivePlacement &a_placement,
                                     std::size_t a_index);
[[nodiscard]] std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, PlacementId a_placement) noexcept;
[[nodiscard]] std::optional<ResolvedPlacement>
ResolvePlacement(const LiveActor &a_state, const LiveGeometry &a_bound,
                 std::size_t a_placed) noexcept;
}
