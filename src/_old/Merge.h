#pragma once

#include "Recipe.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects
{
	struct PlacedRecipe
	{
		const Recipe*            recipe = nullptr;
		int                      priority = 0;
		std::vector<std::size_t> outputs;
	};

	struct Contribution
	{
		std::size_t placed = 0;
		std::size_t output = 0;
		[[nodiscard]] bool operator==(const Contribution&) const = default;
	};

	struct ScalarOwner
	{
		ScalarField  field = ScalarField::kStrength;
		Contribution from;
	};

	struct SlotPlan
	{
		Surface                     surface = Surface::kMaterial;
		Slot                        slot = Slot::kEmissive;
		std::vector<Contribution>   chain;
		std::vector<Contribution>   replaced;
		std::optional<Contribution> replacer;
		std::vector<ScalarOwner>    scalars;
	};

	struct GeometryPlan
	{
		std::vector<SlotPlan> slots;
	};

	struct LightPlan
	{
		std::vector<Contribution>   shown;
		std::vector<Contribution>   replaced;
		std::optional<Contribution> replacer;
	};

	[[nodiscard]] std::optional<std::size_t> ScalarSource(Slot a_slot, ScalarField a_field, std::span<const SurfaceOutput* const> a_outputs);
	[[nodiscard]] GeometryPlan               PlanGeometry(std::span<const PlacedRecipe> a_placed);
	[[nodiscard]] LightPlan       PlanLights(std::span<const PlacedRecipe> a_placed);
	[[nodiscard]] const SlotPlan* SlotPlanOf(const GeometryPlan& a_plan, Surface a_surface, Slot a_slot) noexcept;
	[[nodiscard]] std::optional<Contribution> ScalarOwnerOf(const SlotPlan& a_plan, ScalarField a_field) noexcept;
	[[nodiscard]] std::optional<std::size_t>  ReplacerOf(const SlotPlan& a_plan, Contribution a_contribution) noexcept;
	[[nodiscard]] std::optional<std::size_t>  ReplacerOf(const LightPlan& a_plan, Contribution a_contribution) noexcept;
}
