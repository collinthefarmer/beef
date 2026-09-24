// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Precedence.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
using OutputFilter = std::function<bool(const Recipe &, std::size_t)>;

struct PlacedRecipe {
  const Recipe *recipe = nullptr;
  int priority = 0;
  std::vector<std::size_t> outputs;
  std::size_t loadOrder = 0;
};

enum class SlotContributor : std::size_t {};
enum class LightContributor : std::size_t {};

struct SlotContribution {
  SlotContributor placed{};
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const SlotContribution &) const = default;
};

struct LightContribution {
  LightContributor placed{};
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const LightContribution &) const = default;
};

struct ScalarOwner {
  ScalarField field = ScalarField::kStrength;
  SlotContribution from;
};

struct SlotPlan {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::vector<SlotContribution> chain;
  std::vector<SlotContribution> replaced;
  std::optional<SlotContribution> replacer;
  std::vector<ScalarOwner> scalars;
};

struct GeometryPlan {
  std::vector<SlotPlan> slots;
};

struct LightPlan {
  std::vector<LightContribution> shown;
  std::vector<LightContribution> replaced;
  std::optional<LightContribution> replacer;
};

[[nodiscard]] std::optional<std::size_t>
ScalarSource(Slot a_slot, ScalarField a_field,
             std::span<const SurfaceOutput *const> a_outputs);
[[nodiscard]] GeometryPlan PlanGeometry(std::span<const PlacedRecipe> a_placed);
[[nodiscard]] LightPlan PlanLights(std::span<const PlacedRecipe> a_placed,
                                   const OutputFilter &a_filter = {});
[[nodiscard]] const SlotPlan *
SlotPlanOf(const GeometryPlan &a_plan, Surface a_surface, Slot a_slot) noexcept;
[[nodiscard]] std::optional<SlotContribution>
ScalarOwnerOf(const SlotPlan &a_plan, ScalarField a_field) noexcept;
[[nodiscard]] std::optional<std::size_t>
ReplacerOf(const SlotPlan &a_plan, SlotContribution a_contribution) noexcept;
[[nodiscard]] std::optional<std::size_t>
ReplacerOf(const LightPlan &a_plan, LightContribution a_contribution) noexcept;
}
