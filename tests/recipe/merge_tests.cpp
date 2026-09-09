#include "recipe/Merge.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	const SlotContribution  slot{ SlotSource{ 0 }, 2 };
	const LightContribution light{ LightSource{ 1 }, 0 };

	Check(slot == SlotContribution{ SlotSource{ 0 }, 2 }, "a slot contribution compares by its two fields");
	Check(!(light == LightContribution{ LightSource{ 0 }, 0 }), "a light contribution distinguishes its source index");

	SlotPlan plan;
	plan.slot = Slot::kEmissive;
	plan.chain.push_back(slot);
	Check(plan.chain.size() == 1, "a slot plan holds a chain of contributions");
	Check(plan.scalars.empty(), "a fresh slot plan owns no scalars");

	LightPlan lights;
	lights.shown.push_back(light);
	Check(lights.shown.size() == 1, "a light plan holds the lights shown");

	return test::Finish("merge");
}
