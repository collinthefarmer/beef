#include "mesh/MeshFacts.h"
#include "recipe/Words.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Check(kBipedSlots[2].slot == 32 && kBipedSlots[2].name == "body", "MeshFacts reads slot names from the single table");

	SlotCoverage slot;
	slot.slot = 32;
	slot.name = "body";
	slot.triangles = 10;
	Check(slot == slot, "a slot coverage compares equal to itself");

	BoneCoverage bone;
	bone.name = "NPC Spine";
	bone.coverage = 0.5f;
	Check(test::Near(bone.coverage, 0.5f), "a bone moving half the mesh reads 0.5");

	MeshFacts facts;
	facts.slots.push_back(slot);
	facts.bones.push_back(bone);
	Check(facts.slots.size() == 1 && facts.bones.size() == 1, "facts hold covered slots and moving bones");
	Check(MeshFacts{} == MeshFacts{}, "two empty facts compare equal");

	return test::Finish("meshfacts");
}
