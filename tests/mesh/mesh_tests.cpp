#include "mesh/Mesh.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	MeshVertex vertex;
	vertex.position = Vec3{ 1.0f, 2.0f, 3.0f };
	vertex.uv = Vec2{ 0.25f, 0.75f };
	Check(vertex == vertex, "a vertex compares equal to itself");

	MeshPartition partition;
	Check(partition.slot == MeshPartition::kNoSlot, "a fresh partition names no slot");
	partition.vertices.push_back(vertex);

	MeshData mesh;
	mesh.partitions.push_back(partition);
	Check(mesh.partitions.size() == 1, "a partition is stored");
	Check(MeshData{} == MeshData{}, "two empty meshes compare equal");

	VertexLayout layout;
	Check(!layout.position.has_value() && layout.stride == 0, "a fresh layout has no attributes");

	RawPartition raw;
	Check(raw.vertexCount == 0 && raw.triangleCount == 0 && raw.vertexBytes.empty(), "a raw partition carries sized, empty spans");

	BakeBuffers bake;
	Check(!bake.vector && bake.problem.empty(), "a fresh bake is scalar with no problem");

	Check(kHashBasis == 0xcbf29ce484222325ull, "the FNV-1a basis is exposed");
	Check(test::Near(kPositionFrame, 128.0f) && test::Near(kDistanceFrame, 256.0f), "the bake frames are exposed");

	return test::Finish("mesh");
}
