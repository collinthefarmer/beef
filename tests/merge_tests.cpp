#include "Merge.h"
#include "test_support.h"

#include <string>

using namespace WornEnchantmentPBR;
using test::Check;

namespace
{
	SurfaceOutput Surface_(Surface a_surface, Slot a_slot, bool a_replace = false)
	{
		SurfaceOutput output;
		output.surface = a_surface;
		output.slot = a_slot;
		output.replace = a_replace;
		return output;
	}

	Recipe RecipeNamed(const std::string& a_id)
	{
		Recipe recipe;
		recipe.id = a_id;
		return recipe;
	}

	std::vector<std::size_t> AllSurfaceOutputs(const Recipe& a_recipe)
	{
		std::vector<std::size_t> out;
		for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
			if (Get<SurfaceOutput>(a_recipe.outputs[i])) {
				out.push_back(i);
			}
		}
		return out;
	}

	PlacedRecipe Placed(const Recipe& a_recipe, int a_priority)
	{
		return PlacedRecipe{ &a_recipe, a_priority, AllSurfaceOutputs(a_recipe) };
	}

	void Chains()
	{
		Recipe lower = RecipeNamed("lower");
		lower.outputs.push_back(Surface_(Surface::kShell, Slot::kEmissive));
		Get<SurfaceOutput>(lower.outputs[0])->scalars.strength = 2.0f;
		Recipe higher = RecipeNamed("higher");
		higher.outputs.push_back(Surface_(Surface::kShell, Slot::kEmissive));
		const std::vector placed{ Placed(lower, 30), Placed(higher, 40) };
		const auto        plan = PlanGeometry(placed);
		Check(plan.slots.size() == 1, "two outputs on one slot make one slot plan");
		const auto* slot = SlotPlanOf(plan, Surface::kShell, Slot::kEmissive);
		Check(slot && slot->chain.size() == 2 && slot->chain[0] == Contribution{ 0, 0 } && slot->chain[1] == Contribution{ 1, 0 }, "the chain runs lowest priority first");
		Check(slot && slot->replaced.empty() && !slot->replacer, "nothing replaced without a replace flag");
		Check(slot && ScalarOwnerOf(*slot, ScalarField::kStrength) == Contribution{ 0, 0 }, "a scalar only the lower output names belongs to the lower output");
		Check(SlotPlanOf(plan, Surface::kMaterial, Slot::kEmissive) == nullptr, "the same slot on the other surface is another plan");

		Get<SurfaceOutput>(higher.outputs[0])->scalars.strength = 5.0f;
		const auto both = PlanGeometry(placed);
		const auto* bothSlot = SlotPlanOf(both, Surface::kShell, Slot::kEmissive);
		Check(bothSlot && ScalarOwnerOf(*bothSlot, ScalarField::kStrength) == Contribution{ 1, 0 }, "a scalar both name belongs to the higher output");
		Check(bothSlot && !ScalarOwnerOf(*bothSlot, ScalarField::kColor), "a field nothing names has no owner");

		const std::vector reversed{ Placed(higher, 40), Placed(lower, 30) };
		const auto        sorted = PlanGeometry(reversed);
		const auto*       sortedSlot = SlotPlanOf(sorted, Surface::kShell, Slot::kEmissive);
		Check(sortedSlot && sortedSlot->chain[0] == Contribution{ 1, 0 } && sortedSlot->chain[1] == Contribution{ 0, 0 }, "input order does not matter; priority does");

		const std::vector tied{ Placed(higher, 40), Placed(lower, 40) };
		const auto        tiedPlan = PlanGeometry(tied);
		const auto*       tiedSlot = SlotPlanOf(tiedPlan, Surface::kShell, Slot::kEmissive);
		Check(tiedSlot && tiedSlot->chain[0] == Contribution{ 0, 0 } && tiedSlot->chain[1] == Contribution{ 1, 0 }, "equal priorities keep the input order (load order)");
	}

	void Replaces()
	{
		Recipe lower = RecipeNamed("lower");
		lower.outputs.push_back(Surface_(Surface::kMaterial, Slot::kDiffuse));
		Recipe middle = RecipeNamed("middle");
		middle.outputs.push_back(Surface_(Surface::kMaterial, Slot::kDiffuse, true));
		Recipe higher = RecipeNamed("higher");
		higher.outputs.push_back(Surface_(Surface::kMaterial, Slot::kDiffuse));
		const std::vector placed{ Placed(lower, 10), Placed(middle, 20), Placed(higher, 30) };
		const auto        plan = PlanGeometry(placed);
		const auto*       slot = SlotPlanOf(plan, Surface::kMaterial, Slot::kDiffuse);
		Check(slot && slot->chain.size() == 2 && slot->chain[0] == Contribution{ 1, 0 } && slot->chain[1] == Contribution{ 2, 0 }, "a replace starts the chain at itself; what is above it still stacks");
		Check(slot && slot->replaced.size() == 1 && slot->replaced[0] == Contribution{ 0, 0 } && slot->replacer == Contribution{ 1, 0 }, "what is below the replace is cut and remembers the replacer");
		Check(slot && ReplacerOf(*slot, Contribution{ 0, 0 }) == 0uz + 1 && !ReplacerOf(*slot, Contribution{ 2, 0 }), "the replacer is asked per contribution");

		Get<SurfaceOutput>(higher.outputs[0])->replace = true;
		const auto twice = PlanGeometry(placed);
		const auto* twiceSlot = SlotPlanOf(twice, Surface::kMaterial, Slot::kDiffuse);
		Check(twiceSlot && twiceSlot->chain.size() == 1 && twiceSlot->chain[0] == Contribution{ 2, 0 } && twiceSlot->replaced.size() == 2 && twiceSlot->replacer == Contribution{ 2, 0 }, "the highest replace wins");

		Get<SurfaceOutput>(higher.outputs[0])->replace = false;
		Get<SurfaceOutput>(middle.outputs[0])->replace = false;
		Get<SurfaceOutput>(lower.outputs[0])->replace = true;
		const auto lowest = PlanGeometry(placed);
		const auto* lowestSlot = SlotPlanOf(lowest, Surface::kMaterial, Slot::kDiffuse);
		Check(lowestSlot && lowestSlot->chain.size() == 3 && lowestSlot->replaced.empty() && lowestSlot->replacer == Contribution{ 0, 0 }, "a replace at the bottom cuts nothing");
	}

	void Selections()
	{
		Recipe recipe = RecipeNamed("r");
		recipe.outputs.push_back(Surface_(Surface::kShell, Slot::kEmissive));
		recipe.outputs.push_back(Surface_(Surface::kShell, Slot::kFuzz));
		recipe.outputs.push_back(Surface_(Surface::kMaterial, Slot::kNormal));
		PlacedRecipe placed{ &recipe, 40, { 0, 2 } };
		const auto   plan = PlanGeometry(std::vector{ placed });
		Check(plan.slots.size() == 2 && SlotPlanOf(plan, Surface::kShell, Slot::kEmissive) && SlotPlanOf(plan, Surface::kMaterial, Slot::kNormal) && !SlotPlanOf(plan, Surface::kShell, Slot::kFuzz), "only the outputs listed as matching the geometry are planned");
		PlacedRecipe stale{ &recipe, 40, { 7 } };
		Check(PlanGeometry(std::vector{ stale }).slots.empty(), "an output index past the recipe's outputs is ignored");
		PlacedRecipe none{ nullptr, 40, { 0 } };
		Check(PlanGeometry(std::vector{ none }).slots.empty(), "a placed recipe without a recipe contributes nothing");
		Check(PlanGeometry({}).slots.empty(), "no placements, no plan");
	}

	void Lights()
	{
		Recipe lower = RecipeNamed("lower");
		lower.outputs.push_back(Surface_(Surface::kShell, Slot::kEmissive));
		lower.outputs.push_back(LightOutput{});
		Recipe higher = RecipeNamed("higher");
		higher.outputs.push_back(LightOutput{});
		Recipe dark = RecipeNamed("dark");
		dark.outputs.push_back(Surface_(Surface::kShell, Slot::kEmissive));
		const std::vector placed{ Placed(lower, 30), Placed(higher, 40), Placed(dark, 50) };
		const auto        plan = PlanLights(placed);
		Check(plan.shown.size() == 2 && plan.shown[0] == Contribution{ 0, 1 } && plan.shown[1] == Contribution{ 1, 0 } && plan.replaced.empty(), "every light shows without a replace; a recipe without a light is not listed");
		Get<LightOutput>(higher.outputs[0])->replace = true;
		const auto replaced = PlanLights(placed);
		Check(replaced.shown.size() == 1 && replaced.shown[0] == Contribution{ 1, 0 } && replaced.replaced.size() == 1 && replaced.replaced[0] == Contribution{ 0, 1 }, "a replacing light hides the lower lights");
		Check(ReplacerOf(replaced, Contribution{ 0, 1 }) == 0uz + 1 && !ReplacerOf(replaced, Contribution{ 1, 0 }), "the light's replacer is asked per contribution");
	}
}

int main()
{
	Chains();
	Replaces();
	Selections();
	Lights();
	return test::Finish("merge");
}
