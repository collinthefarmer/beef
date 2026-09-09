#include "Merge.h"

#include <algorithm>

namespace WornEnchantmentPBR
{
	namespace
	{
		std::vector<std::size_t> PriorityOrder(std::span<const PlacedRecipe> a_placed)
		{
			std::vector<std::size_t> order(a_placed.size());
			for (std::size_t i = 0; i < order.size(); ++i) {
				order[i] = i;
			}
			std::ranges::stable_sort(order, [&](std::size_t a_lhs, std::size_t a_rhs) { return a_placed[a_lhs].priority < a_placed[a_rhs].priority; });
			return order;
		}

		const SurfaceOutput* SurfaceOutputAt(const PlacedRecipe& a_placed, std::size_t a_index) noexcept
		{
			if (!a_placed.recipe || a_index >= a_placed.recipe->outputs.size()) {
				return nullptr;
			}
			return Get<SurfaceOutput>(a_placed.recipe->outputs[a_index]);
		}

		bool NamesScalar(const SlotScalars& a_scalars, ScalarField a_field) noexcept
		{
			if (a_field == ScalarField::kColor) {
				return a_scalars.color.has_value();
			}
			const auto* param = ScalarOf(a_scalars, a_field);
			return param && param->has_value();
		}

		struct Flagged
		{
			Contribution contribution;
			bool         replaces = false;
		};

		void CutAtReplace(std::span<const Flagged> a_flagged, std::vector<Contribution>& a_ordered, std::vector<Contribution>& a_replaced, std::optional<Contribution>& a_replacer)
		{
			std::size_t start = 0;
			for (std::size_t i = 0; i < a_flagged.size(); ++i) {
				if (a_flagged[i].replaces) {
					start = i;
					a_replacer = a_flagged[i].contribution;
				}
			}
			a_replaced.assign(a_ordered.begin(), a_ordered.begin() + static_cast<std::ptrdiff_t>(start));
			a_ordered.erase(a_ordered.begin(), a_ordered.begin() + static_cast<std::ptrdiff_t>(start));
		}
	}

	GeometryPlan PlanGeometry(std::span<const PlacedRecipe> a_placed)
	{
		GeometryPlan plan;
		for (const auto placed : PriorityOrder(a_placed)) {
			for (const auto index : a_placed[placed].outputs) {
				const auto* output = SurfaceOutputAt(a_placed[placed], index);
				if (!output) {
					continue;
				}
				auto slot = std::ranges::find_if(plan.slots, [&](const SlotPlan& a_slot) { return a_slot.surface == output->surface && a_slot.slot == output->slot; });
				if (slot == plan.slots.end()) {
					plan.slots.push_back(SlotPlan{ output->surface, output->slot, {}, {}, std::nullopt, {} });
					slot = std::prev(plan.slots.end());
				}
				slot->chain.push_back(Contribution{ placed, index });
			}
		}
		for (auto& slot : plan.slots) {
			std::vector<Flagged> flagged;
			for (const auto& c : slot.chain) {
				const auto* output = SurfaceOutputAt(a_placed[c.placed], c.output);
				flagged.push_back(Flagged{ c, output && output->replace });
			}
			CutAtReplace(flagged, slot.chain, slot.replaced, slot.replacer);
			for (const auto field : ScalarsOf(slot.slot)) {
				for (auto it = slot.chain.rbegin(); it != slot.chain.rend(); ++it) {
					const auto* output = SurfaceOutputAt(a_placed[it->placed], it->output);
					if (output && NamesScalar(output->scalars, field)) {
						slot.scalars.push_back(ScalarOwner{ field, *it });
						break;
					}
				}
			}
		}
		return plan;
	}

	LightPlan PlanLights(std::span<const PlacedRecipe> a_placed)
	{
		LightPlan            plan;
		std::vector<Flagged> flagged;
		for (const auto placed : PriorityOrder(a_placed)) {
			const auto* recipe = a_placed[placed].recipe;
			if (!recipe) {
				continue;
			}
			for (std::size_t i = 0; i < recipe->outputs.size(); ++i) {
				if (const auto* light = Get<LightOutput>(recipe->outputs[i])) {
					plan.shown.push_back(Contribution{ placed, i });
					flagged.push_back(Flagged{ Contribution{ placed, i }, light->replace });
					break;
				}
			}
		}
		CutAtReplace(flagged, plan.shown, plan.replaced, plan.replacer);
		return plan;
	}

	const SlotPlan* SlotPlanOf(const GeometryPlan& a_plan, Surface a_surface, Slot a_slot) noexcept
	{
		for (const auto& slot : a_plan.slots) {
			if (slot.surface == a_surface && slot.slot == a_slot) {
				return &slot;
			}
		}
		return nullptr;
	}

	std::optional<Contribution> ScalarOwnerOf(const SlotPlan& a_plan, ScalarField a_field) noexcept
	{
		for (const auto& owner : a_plan.scalars) {
			if (owner.field == a_field) {
				return owner.from;
			}
		}
		return std::nullopt;
	}

	std::optional<std::size_t> ReplacerOf(const SlotPlan& a_plan, Contribution a_contribution) noexcept
	{
		if (a_plan.replacer && std::ranges::find(a_plan.replaced, a_contribution) != a_plan.replaced.end()) {
			return a_plan.replacer->placed;
		}
		return std::nullopt;
	}

	std::optional<std::size_t> ReplacerOf(const LightPlan& a_plan, Contribution a_contribution) noexcept
	{
		if (a_plan.replacer && std::ranges::find(a_plan.replaced, a_contribution) != a_plan.replaced.end()) {
			return a_plan.replacer->placed;
		}
		return std::nullopt;
	}
}
