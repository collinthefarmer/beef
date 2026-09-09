#include "mesh/MeshFacts.h"
#include "mesh/Mesh.h"
#include "recipe/Words.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace
{
	MeshVertex Weighted(std::uint16_t a_bone, float a_weight)
	{
		MeshVertex vertex;
		vertex.bones[0] = a_bone;
		vertex.weights[0] = a_weight;
		return vertex;
	}

	MeshPartition Slotted(std::uint16_t a_slot, std::size_t a_triangles)
	{
		MeshPartition partition;
		partition.slot = a_slot;
		partition.triangles.assign(a_triangles, std::array<std::uint32_t, 3>{ 0, 0, 0 });
		return partition;
	}
}

int main()
{
	Check(kBipedSlots[2].slot == 32 && kBipedSlots[2].name == "body", "MeshFacts reads slot names from the single table");

	{
		MeshData mesh;
		mesh.partitions.push_back(Slotted(32, 3));
		mesh.partitions.push_back(Slotted(33, 2));
		mesh.partitions.push_back(Slotted(32, 1));
		mesh.partitions.push_back(Slotted(MeshPartition::kNoSlot, 5));
		mesh.partitions.push_back(Slotted(99, 4));

		const std::vector<SlotCoverage> slots = SlotsOf(mesh);
		Check(slots.size() == 3, "an unslotted partition is left out of the coverage");
		Check(slots[0].slot == 32 && slots[0].name == "body" && slots[0].triangles == 4, "two partitions in one slot accumulate their triangles under the slot's name");
		Check(slots[1].slot == 33 && slots[1].name == "hands" && slots[1].triangles == 2, "a second slot keeps its own name and count");
		Check(slots[2].slot == 99 && slots[2].name == "99", "a slot outside the table falls back to its number");
	}

	{
		MeshData mesh;
		MeshPartition partition;
		partition.slot = 32;
		partition.boneNames = { "NPC Spine", "NPC Pelvis" };
		partition.vertices = { Weighted(0, 1.0f), Weighted(0, 1.0f), Weighted(1, 1.0f) };
		mesh.partitions.push_back(partition);

		const std::vector<BoneCoverage> bones = BonesOf(mesh);
		Check(bones.size() == 2, "each weighted bone appears once");
		Check(bones[0].name == "NPC Spine" && test::Near(bones[0].coverage, 2.0f / 3.0f), "the bone that moves the most vertices sorts first with its share of the mesh");
		Check(bones[1].name == "NPC Pelvis" && test::Near(bones[1].coverage, 1.0f / 3.0f), "a lesser bone follows with its smaller share");
	}

	{
		MeshData mesh;
		MeshPartition partition;
		partition.slot = 32;
		partition.boneNames = { "NPC Spine" };
		partition.vertices = { Weighted(0, 1.0f), Weighted(1, 1.0f) };
		mesh.partitions.push_back(partition);

		const std::vector<BoneCoverage> bones = BonesOf(mesh);
		Check(bones.size() == 1 && bones[0].name == "NPC Spine", "a weight on a bone index past the partition's bone list is ignored");
		Check(test::Near(bones[0].coverage, 0.5f), "a bone moving one of two vertices reads 0.5, the out-of-range vertex still counting in the whole");
	}

	{
		MeshData empty;
		Check(SlotsOf(empty).empty() && BonesOf(empty).empty(), "a mesh with no partitions has no facts");
		const MeshFacts facts = FactsOf(empty);
		Check(facts == MeshFacts{}, "facts of an empty mesh compare equal to empty facts");
	}

	{
		MeshData mesh;
		mesh.partitions.push_back(Slotted(37, 1));
		MeshPartition partition;
		partition.slot = 37;
		partition.boneNames = { "NPC L Foot" };
		partition.vertices = { Weighted(0, 1.0f) };
		mesh.partitions.push_back(partition);

		const MeshFacts facts = FactsOf(mesh);
		Check(facts.slots == SlotsOf(mesh) && facts.bones == BonesOf(mesh), "FactsOf pairs the slot coverage with the bone coverage");
		Check(facts.slots.size() == 1 && facts.slots[0].name == "feet", "the paired facts carry the feet slot");
	}

	return test::Finish("meshfacts");
}
