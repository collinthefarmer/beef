#include "Mesh.h"
#include "test_support.h"

#include <cstring>
#include <span>
#include <string>

using namespace BetterEnchantmentEffects;

namespace
{
	std::uint16_t FloatToHalf(float a_value)
	{
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
			v[20] = 127;
			v[21] = 127;
			v[22] = 255;
			const std::uint16_t weights[4]{ FloatToHalf(0.75f), FloatToHalf(0.25f), 0, 0 };
			std::memcpy(v + 24, weights, 8);
			v[32] = 0;
			v[33] = 1;
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
		tooShort.stride = 8;
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

namespace
{
	void Hashing()
	{
		using namespace test;
		const std::string foobar = "foobar";
		const auto        bytes = [](const std::string& a_text) { return std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(a_text.data()), a_text.size() }; };
		Check(HashBytes({}) == 0xcbf29ce484222325ull, "the empty hash is the FNV basis");
		Check(HashBytes(bytes("a")) == 0xaf63dc4c8601ec8cull, "one byte hashes as FNV-1a");
		Check(HashBytes(bytes(foobar)) == 0x85944171f73967e8ull, "a known string hashes as FNV-1a");
		Check(HashBytes(bytes("bar"), HashBytes(bytes("foo"))) == HashBytes(bytes(foobar)), "chaining through the seed hashes the buffers as one");
		Check(HashBytes(bytes("foo")) != HashBytes(bytes("fop")), "one changed byte changes the hash");
	}

	void Triangles()
	{
		using namespace test;
		const std::vector<std::array<std::uint32_t, 3>> triangles{ { 0, 1, 2 }, { 0, 2, 9 }, { 3, 1, 2 }, { 0, 0, 3 } };
		const auto                                      kept = TrianglesWithin(triangles, 3);
		Check(kept.size() == 1 && kept[0] == std::array<std::uint32_t, 3>{ 0, 1, 2 }, "a triangle that indexes past the vertices is dropped");
		Check(TrianglesWithin(triangles, 4).size() == 3, "a triangle whose largest index is the last vertex is kept");
		Check(TrianglesWithin(triangles, 0).empty(), "no vertices keeps nothing");
		MeshData mesh = Quad();
		auto&    partition = mesh.partitions[0];
		partition.triangles = { { 0, 1, 2 }, { 0, 2, 3 }, { 1, 2, 7 }, { 4, 5, 6 } };
		partition.triangles = TrianglesWithin(partition.triangles, partition.vertices.size());
		Check(partition.triangles.size() == 2, "the partition keeps its two triangles inside the vertices");
		const auto bake = BuildBake(mesh, PartitionBake{ 32 });
		Check(bake.indices.size() == 6, "the bake draws the kept triangles alone");
	}

	void Sizes()
	{
		using namespace test;
		Check(TextureSize::Clamp(0).Pixels() == 64, "0 px clamps up to the minimum");
		Check(TextureSize::Clamp(64).Pixels() == 64, "the minimum is kept");
		Check(TextureSize::Clamp(4096).Pixels() == 4096, "the maximum is kept");
		Check(TextureSize::Clamp(5000).Pixels() == 4096, "5000 px clamps down to the maximum");
		Check(TextureSize::Clamp(512) == TextureSize::Clamp(512), "two clamps of one size compare equal");
		Check(TextureSize::Clamp(0) == TextureSize::Clamp(64), "a clamped size equals the bound it landed on");
		Check(!(TextureSize::Clamp(512) == TextureSize::Clamp(1024)), "two sizes differ");
	}

	void Keys()
	{
		using namespace test;
		const TextureSize s64 = TextureSize::Clamp(64);
		const TextureSize s128 = TextureSize::Clamp(128);
		const TextureSize s256 = TextureSize::Clamp(256);
		const TextureSize s512 = TextureSize::Clamp(512);
		const TextureSize s1024 = TextureSize::Clamp(1024);
		const TextureSize s4096 = TextureSize::Clamp(4096);
		Check(BakeKeyOf(PositionBake{}, s512) == "bake position@512", "a position bake keys by kind and size");
		Check(BakeKeyOf(PositionBake{}, s512) != BakeKeyOf(PositionBake{}, s1024), "sizes differ");
		Check(BakeKeyOf(PositionBake{}, s512) != BakeKeyOf(LocalPositionBake{}, s512), "kinds differ");
		Check(BakeKeyOf(PartitionBake{ 32 }, s512) != BakeKeyOf(PartitionBake{ 33 }, s512), "partitions differ by slot");
		const BakeKind left = BoneWeightBake{ { "NPC L Clavicle [LClv]" } };
		const BakeKind right = BoneWeightBake{ { "NPC R Clavicle [RClv]" } };
		Check(BakeKeyOf(left, s512) != BakeKeyOf(right, s512), "bone lists of one length differ by name");
		Check(BakeKeyOf(left, s512) == BakeKeyOf(BakeKind{ BoneWeightBake{ { "NPC L Clavicle [LClv]" } } }, s512), "the same definition under two names is one key");
		Check(BakeKeyOf(BakeKind{ BoneWeightBake{ { "a", "b" } } }, s512) == BakeKeyOf(BakeKind{ BoneWeightBake{ { "b", "a" } } }, s512), "one set of bones in two orders is one key");
		Check(BakeKeyOf(BakeKind{ BoneWeightBake{ { "a", "b" } } }, s512) != BakeKeyOf(BakeKind{ BoneWeightBake{ { "a" } } }, s512), "a subset of the bones is another key");
		const DistanceSource head{ std::string{ "NPC Head [Head]" } };
		Check(DistanceKeyOf(head, s256) == "distance from node NPC Head [Head]@256", "a distance from a node keys by the node");
		Check(DistanceKeyOf(DistanceSource{ Vec3{ 1.0f, 2.0f, 3.0f } }, s256) != DistanceKeyOf(DistanceSource{ Vec3{ 1.0f, 2.0f, 3.5f } }, s256), "distances from two points differ");
		Check(UvKeyOf(UvAxis::kU, s128) == "uv u@128" && UvKeyOf(UvAxis::kV, s128) == "uv v@128", "uv keys by axis");
		Check(KeyDefinition("bake position@512") == "bake position" && KeySize("bake position@512") == 512u, "a key splits into its definition and size");
		Check(KeyDefinition(BakeKeyOf(left, s64)) == KeyDefinition(BakeKeyOf(left, s4096)), "the definition is the same at every size");
		Check(!KeySize("bake position").has_value() && !KeySize("bake position@").has_value() && !KeySize("bake position@12x").has_value(), "a key without a whole number has no size");
		Check(DefinitionOf(left) == KeyDefinition(BakeKeyOf(left, s512)), "a bake's definition is the definition half of its key");
		Check(DefinitionOf(head) == KeyDefinition(DistanceKeyOf(head, s256)), "a distance's definition is the definition half of its key");
		Check(DefinitionOf(UvAxis::kV) == KeyDefinition(UvKeyOf(UvAxis::kV, s128)), "a uv axis's definition is the definition half of its key");
		Check(DefinitionOf(left).find('@') == std::string::npos, "a definition never contains '@'");
	}
}

namespace
{
	void RegionBakes()
	{
		using namespace test;
		const auto mesh = Quad();
		const auto pieces = BuildBake(mesh, ComponentIdBake{});
		Check(pieces.problem == "componentId needs the mesh analysis" && pieces.vertices.empty() && pieces.indices.empty() && !pieces.vector, "a componentId bake from the mesh alone is a problem");
		const auto charts = BuildBake(mesh, ChartIdBake{});
		Check(charts.problem == "chartId needs the mesh analysis" && charts.vertices.empty() && charts.indices.empty(), "a chartId bake from the mesh alone is a problem");
		Check(DefinitionOf(BakeKind{ ComponentIdBake{} }) == "bake componentId" && DefinitionOf(BakeKind{ ChartIdBake{} }) == "bake chartId", "the id maps define by their names");
		Check(BakeKeyOf(ComponentIdBake{}, TextureSize::Clamp(256)) == "bake componentId@256" && BakeKeyOf(ChartIdBake{}, TextureSize::Clamp(256)) != BakeKeyOf(ComponentIdBake{}, TextureSize::Clamp(256)), "an id map keys by name and size");
	}
}

int main()
{
	Decoding();
	Bakes();
	RegionBakes();
	Hashing();
	Triangles();
	Sizes();
	Keys();
	return test::Finish("bake");
}
