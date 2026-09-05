#pragma once

// A mesh as the bakes read it: bind-pose vertices with UVs, normals and
// bone weights, grouped by skin partition. Engine-free: the reader in
// MeshReader.cpp fills it from the engine's buffers, BuildBake turns it and
// a bake kind into the triangles the lab rasterises into UV space.

#include "Core.h"
#include "Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace WornEnchantmentPBR
{
	struct MeshVertex
	{
		Vec3                         position;  // bind pose, model units
		Vec3                         normal;    // bind pose, unit length when the mesh has normals
		Vec2                         uv;
		std::array<std::uint16_t, 4> bones{};    // into MeshPartition::boneNames
		std::array<float, 4>         weights{};  // sum to 1 on a skinned vertex
	};

	// One skin partition, or the whole mesh when it is not skinned.
	struct MeshPartition
	{
		static constexpr std::uint16_t kNoSlot = 0xFFFF;

		std::uint16_t                             slot = kNoSlot;  // biped slot number
		std::vector<std::string>                  boneNames;
		std::vector<MeshVertex>                   vertices;
		std::vector<std::array<std::uint32_t, 3>> triangles;  // into vertices
	};

	struct MeshData
	{
		Vec3                       center;  // the model bound
		float                      radius = 0.0f;
		std::vector<MeshPartition> partitions;
		std::string                origin;  // "cpu copy" or "gpu readback", for the log
	};

	// ------------------------------------------------- packed vertex bytes
	// The engine keeps one vertex as: position 4 floats (xyz and a tangent
	// component), uv 2 halfs, normal 4 bytes (xyz biased, 1 tangent), skinning
	// 4 half weights then 4 byte bone indices. Offsets come from the vertex
	// descriptor; an attribute the mesh lacks has no offset.

	struct VertexLayout
	{
		std::uint32_t                stride = 0;
		std::optional<std::uint32_t> position;
		std::optional<std::uint32_t> uv;
		std::optional<std::uint32_t> normal;
		std::optional<std::uint32_t> skinning;
	};

	[[nodiscard]] float HalfToFloat(std::uint16_t a_half) noexcept;

	// Empty when the bytes are too short for the vertex or the layout has no position.
	[[nodiscard]] std::optional<MeshVertex> DecodeVertex(std::span<const std::uint8_t> a_bytes, const VertexLayout& a_layout, std::size_t a_index) noexcept;

	// --------------------------------------------------------------- bakes

	struct BakeVertex
	{
		float u = 0.0f;
		float v = 0.0f;
		float value[3]{};
	};

	// Triangles in UV space carrying the baked value; empty with a problem
	// when the bake has nothing to draw.
	struct BakeBuffers
	{
		std::vector<BakeVertex>    vertices;
		std::vector<std::uint32_t> indices;
		bool                       vector = false;  // the value is a colour (position), else a scalar in x
		std::string                problem;
	};

	// position: the bind-pose position in one fixed frame shared by every
	// geometry and piece, -kPositionFrame..kPositionFrame units per axis
	// mapped to 0..1 (the skeleton root is the origin, z up, so a standing
	// actor's body spans about 0.5 to 1 in z). worldUp: how far the bind-pose
	// normal points up, 0..1. partition: 1 on the triangles of the biped
	// slot. boneWeight: the summed weight of the named bones per vertex.
	// localPosition: the position mapped into this geometry's own model
	// bound, 0..1 per axis, so every piece spans the full range on its own.
	inline constexpr float kPositionFrame = 128.0f;
	[[nodiscard]] BakeBuffers BuildBake(const MeshData& a_mesh, const BakeKind& a_kind);

	// The bind-pose distance of every vertex from a point, in units, as
	// 0..kDistanceFrame mapped to 0..1; a scalar.
	inline constexpr float kDistanceFrame = 256.0f;
	[[nodiscard]] BakeBuffers BuildDistanceBake(const MeshData& a_mesh, const Vec3& a_from);

	// One texture coordinate of every vertex as its value, so the islands
	// carry a u or v ramp; a scalar.
	[[nodiscard]] BakeBuffers BuildUvBake(const MeshData& a_mesh, UvAxis a_axis);
}
