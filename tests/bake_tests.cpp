#include "Mesh.h"
#include "test_support.h"

#include <cstring>

using namespace WornEnchantmentPBR;

namespace
{
	std::uint16_t FloatToHalf(float a_value)
	{
		// Enough for the test values: normal numbers only.
		std::uint32_t bits;
		std::memcpy(&bits, &a_value, 4);
		const std::uint32_t sign = (bits >> 16) & 0x8000u;
		const std::int32_t  exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
		const std::uint32_t mantissa = (bits >> 13) & 0x3FFu;
		if (exponent <= 0) {
			return static_cast<std::uint16_t>(sign);
		}
		return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | mantissa);
	}

	// A quad in the engine's packing: position, uv, normal, skinning.
	std::vector<std::uint8_t> PackedQuad(VertexLayout& a_layout)
	{
		a_layout.stride = 16 + 4 + 4 + 12;
		a_layout.position = 0;
		a_layout.uv = 16;
		a_layout.normal = 20;
		a_layout.skinning = 24;
		std::vector<std::uint8_t> bytes(a_layout.stride * 4, 0);
		const float               positions[4][3]{ { -1, 0, -1 }, { 1, 0, -1 }, { 1, 0, 1 }, { -1, 0, 1 } };
		const float               uvs[4][2]{ { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
		for (std::size_t i = 0; i < 4; ++i) {
			auto* v = bytes.data() + i * a_layout.stride;
			std::memcpy(v, positions[i], 12);
			const std::uint16_t u = FloatToHalf(uvs[i][0]), w = FloatToHalf(uvs[i][1]);
			std::memcpy(v + 16, &u, 2);
			std::memcpy(v + 18, &w, 2);
			v[20] = 127;  // nx 0
			v[21] = 127;  // ny 0
			v[22] = 255;  // nz 1
			const std::uint16_t weights[4]{ FloatToHalf(0.75f), FloatToHalf(0.25f), 0, 0 };
			std::memcpy(v + 24, weights, 8);
			v[32] = 0;  // bone 0
			v[33] = 1;  // bone 1
		}
		return bytes;
	}

	MeshData Quad()
	{
		VertexLayout layout;
		const auto   bytes = PackedQuad(layout);
		MeshData     mesh;
		mesh.center = Vec3{ 0, 0, 0 };
		mesh.radius = 2.0f;
		MeshPartition p;
		p.slot = 32;
		p.boneNames = { "NPC Spine2 [Spn2]", "NPC Pelvis [Pelv]" };
		for (std::size_t i = 0; i < 4; ++i) {
			p.vertices.push_back(*DecodeVertex(bytes, layout, i));
		}
		p.triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
		mesh.partitions.push_back(p);
		MeshPartition other;
		other.slot = 33;
		other.vertices = { p.vertices[0], p.vertices[1], p.vertices[2] };
		other.triangles = { { 0, 1, 2 } };
		mesh.partitions.push_back(other);
		return mesh;
	}

	void Decoding()
	{
		using namespace test;
		Check(Near(HalfToFloat(0x3C00), 1.0f), "half 1.0 decodes");
		Check(Near(HalfToFloat(0x3800), 0.5f), "half 0.5 decodes");
		Check(Near(HalfToFloat(0xC000), -2.0f), "half -2 decodes");
		Check(Near(HalfToFloat(0x0000), 0.0f), "half zero decodes");
		Check(Near(HalfToFloat(0x0001), 5.9604645e-8f, 1e-12f), "the smallest subnormal decodes");
		VertexLayout layout;
		const auto   bytes = PackedQuad(layout);
		const auto   v = DecodeVertex(bytes, layout, 2);
		Check(v.has_value(), "a vertex inside the buffer decodes");
		if (v) {
			Check(Near(v->position.x, 1.0f) && Near(v->position.z, 1.0f), "the position reads as floats");
			Check(Near(v->uv.x, 1.0f) && Near(v->uv.y, 0.0f), "the uv reads as halfs");
			Check(Near(v->normal.z, 1.0f, 0.01f), "the normal reads biased bytes");
			Check(Near(v->weights[0], 0.75f) && Near(v->weights[1], 0.25f), "the weights read as halfs");
			Check(v->bones[0] == 0 && v->bones[1] == 1, "the bone indices read as bytes");
		}
		Check(!DecodeVertex(bytes, layout, 4).has_value(), "a vertex past the buffer is refused");
		VertexLayout noPosition;
		noPosition.stride = 8;
		Check(!DecodeVertex(bytes, noPosition, 0).has_value(), "a layout without a position is refused");
		VertexLayout tooShort = layout;
		tooShort.stride = 8;  // the position no longer fits
		Check(!DecodeVertex(bytes, tooShort, 0).has_value(), "an attribute past the stride is refused");
	}

	void Bakes()
	{
		using namespace test;
		const auto mesh = Quad();
		const auto position = BuildBake(mesh, PositionBake{});
		Check(position.problem.empty() && position.vector, "the position bake is a colour");
		Check(position.vertices.size() == 7 && position.indices.size() == 9, "both partitions bake for a position");
		if (position.vertices.size() == 7) {
			const float one = 1.0f / (2.0f * kPositionFrame) + 0.5f;
			Check(Near(position.vertices[2].value[0], one) && Near(position.vertices[2].value[2], one), "positions map through the fixed frame, 0..1");
			Check(Near(position.vertices[0].value[0], 1.0f - one), "the frame is symmetric about the root");
			Check(Near(position.vertices[2].u, 1.0f) && Near(position.vertices[2].v, 0.0f), "the uv rides along");
		}
		const auto local = BuildBake(mesh, LocalPositionBake{});
		Check(local.problem.empty() && local.vector, "the local position bake is a colour");
		if (local.vertices.size() == 7) {
			Check(Near(local.vertices[2].value[0], 0.75f) && Near(local.vertices[2].value[2], 0.75f), "local positions map into the bound, 0..1");
		}
		MeshData unbounded = mesh;
		unbounded.radius = 0.0f;
		Check(!BuildBake(unbounded, LocalPositionBake{}).problem.empty(), "a local position bake needs the bound");
		const auto up = BuildBake(mesh, WorldUpBake{});
		Check(!up.vector && Near(up.vertices.front().value[0], 1.0f, 0.01f), "a normal pointing up bakes 1");
		const auto part = BuildBake(mesh, PartitionBake{ 33 });
		Check(part.problem.empty() && part.vertices.size() == 3 && part.indices.size() == 3, "a partition bake draws that slot alone");
		Check(Near(part.vertices.front().value[0], 1.0f), "a partition bake is 1 on its triangles");
		const auto missing = BuildBake(mesh, PartitionBake{ 40 });
		Check(!missing.problem.empty() && missing.vertices.empty(), "a slot the mesh lacks is a problem");
		const auto weight = BuildBake(mesh, BoneWeightBake{ { "NPC Pelvis [Pelv]" } });
		Check(Near(weight.vertices.front().value[0], 0.25f), "a bone weight bake sums the named bones");
		const auto both = BuildBake(mesh, BoneWeightBake{ { "NPC Pelvis [Pelv]", "NPC Spine2 [Spn2]" } });
		Check(Near(both.vertices.front().value[0], 1.0f), "two named bones sum to the full weight");
		const auto none = BuildBake(mesh, BoneWeightBake{ { "NPC Head [Head]" } });
		Check(Near(none.vertices.front().value[0], 0.0f), "an unknown bone weighs nothing");
		const auto dist = BuildDistanceBake(mesh, Vec3{ -1.0f, 0.0f, -1.0f });
		Check(dist.problem.empty() && !dist.vector && dist.vertices.size() == 7, "a distance bake covers every partition as a scalar");
		Check(Near(dist.vertices[0].value[0], 0.0f), "the vertex at the point is at distance 0");
		Check(Near(dist.vertices[2].value[0], std::sqrt(8.0f) / kDistanceFrame), "distance is in units over the frame");
		const auto u = BuildUvBake(mesh, UvAxis::kU);
		Check(u.problem.empty() && Near(u.vertices[1].value[0], 1.0f) && Near(u.vertices[0].value[0], 0.0f), "a u bake is the u coordinate");
		const auto vv = BuildUvBake(mesh, UvAxis::kV);
		Check(Near(vv.vertices[0].value[0], 1.0f) && Near(vv.vertices[2].value[0], 0.0f), "a v bake is the v coordinate");
		MeshData flat = mesh;
		flat.radius = 0.0f;
		Check(BuildBake(flat, PositionBake{}).problem.empty(), "a position bake does not need the bound");
		MeshData far = mesh;
		far.partitions[0].vertices[0].position = Vec3{ 500.0f, 0.0f, -500.0f };
		const auto clamped = BuildBake(far, PositionBake{});
		Check(Near(clamped.vertices[0].value[0], 1.0f) && Near(clamped.vertices[0].value[2], 0.0f), "positions past the frame clamp");
		MeshData bad = mesh;
		bad.partitions[0].triangles = { { 0, 1, 9 } };
		bad.partitions.pop_back();
		Check(!BuildBake(bad, PartitionBake{ 32 }).problem.empty(), "a triangle past the vertices is dropped, leaving nothing");
	}
}

int main()
{
	Decoding();
	Bakes();
	return test::Finish("bake");
}
