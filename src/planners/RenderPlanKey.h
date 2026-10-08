// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorMatches.h"
#include "planners/RenderPlan.h"
#include "planners/ValueIdentity.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct StackKeyRequest {
  const RecipeGraph *graph = nullptr;
  std::size_t output = 0;
  PlacementId placement{};
  GeometryId geometry{};
  std::size_t context = 0;
  SideRef side;
  std::string material;
  std::optional<std::string> meshHash;
  ValueBindings bindings;
};

struct StackKey {
  const RecipeGraph *graph = nullptr;
  std::size_t output = 0;
  PlacementId placement{};
  GeometryId geometry{};
  std::size_t context = 0;
  SideRef side;
  std::string material;
  std::string meshHash;
  std::vector<std::string> staticInputs;
  [[nodiscard]] bool operator==(const StackKey &) const = default;
};

struct ChainKey {
  GeometryId geometry{};
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<ChainLink> links;
  [[nodiscard]] bool operator==(const ChainKey &) const = default;
};

struct RenderPlanKey {
  std::vector<StackKey> stacks;
  std::vector<ChainKey> chains;
  [[nodiscard]] bool operator==(const RenderPlanKey &) const = default;
};

struct RenderPlanKeyHash {
  [[nodiscard]] std::size_t
  operator()(const RenderPlanKey &a_key) const noexcept;
};

[[nodiscard]] std::optional<RenderPlanKey>
RenderPlanKeyOf(std::span<const StackKeyRequest> a_stacks,
                std::span<const SlotChainRequest> a_chains);
[[nodiscard]] std::vector<std::shared_ptr<const RecipeGraph>>
GraphsNamedBy(const RenderPlanKey &a_key,
              std::span<const std::shared_ptr<const RecipeGraph>> a_graphs);
}
