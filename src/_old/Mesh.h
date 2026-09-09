#pragma once

#include "Core.h"
#include "Recipe.h"
#include "TextureSize.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects
{
	struct MeshVertex
	{
		Vec3                         position;
		Vec3                         normal;
		Vec2                         uv;
		std::array<std::uint16_t, 4> bones{};
		std::array<float, 4>         weights{};
	};

	struct MeshPartition
	{
		static constexpr std::uint16_t kNoSlot = 0xFFFF;

		std::uint16_t                             slot = kNoSlot;
		std::vector<std::string>                  boneNames;
		std::vector<MeshVertex>                   vertices;
		std::vector<std::array<std::uint32_t, 3>> triangles;
	};

	struct MeshData
	{
		Vec3                       center;
		float                      radius = 0.0f;
		std::vector<MeshPartition> partitions;
		std::string                origin;
		std::uint64_t              hash = 0;
	};

	inline constexpr std::uint64_t kHashBasis = 0xcbf29ce484222325ull;
	[[nodiscard]] std::uint64_t HashBytes(std::span<const std::uint8_t> a_bytes, std::uint64_t a_seed = kHashBasis) noexcept;

	[[nodiscard]] std::vector<std::array<std::uint32_t, 3>> TrianglesWithin(std::span<const std::array<std::uint32_t, 3>> a_triangles, std::size_t a_vertexCount);

	[[nodiscard]] std::string DefinitionOf(const BakeKind& a_kind);
	[[nodiscard]] std::string DefinitionOf(const DistanceSource& a_distance);
	[[nodiscard]] std::string DefinitionOf(UvAxis a_axis);
	[[nodiscard]] std::string BakeKeyOf(const BakeKind& a_kind, TextureSize a_size);
	[[nodiscard]] std::string DistanceKeyOf(const DistanceSource& a_distance, TextureSize a_size);
	[[nodiscard]] std::string UvKeyOf(UvAxis a_axis, TextureSize a_size);
	[[nodiscard]] std::string_view              KeyDefinition(std::string_view a_key) noexcept;
	[[nodiscard]] std::optional<std::uint32_t> KeySize(std::string_view a_key) noexcept;

	struct VertexLayout
	{
		std::uint32_t                stride = 0;
		std::optional<std::uint32_t> position;
		std::optional<std::uint32_t> uv;
		std::optional<std::uint32_t> normal;
		std::optional<std::uint32_t> skinning;
	};

	[[nodiscard]] float HalfToFloat(std::uint16_t a_half) noexcept;

	[[nodiscard]] std::optional<MeshVertex> DecodeVertex(std::span<const std::uint8_t> a_bytes, const VertexLayout& a_layout, std::size_t a_index) noexcept;

	struct BakeVertex
	{
		float u = 0.0f;
		float v = 0.0f;
		float value[3]{};
	};

	struct BakeBuffers
	{
		std::vector<BakeVertex>    vertices;
		std::vector<std::uint32_t> indices;
		bool                       vector = false;
		std::string                problem;
	};

	inline constexpr float kPositionFrame = 128.0f;
	[[nodiscard]] BakeBuffers BuildBake(const MeshData& a_mesh, const BakeKind& a_kind);

	inline constexpr float kDistanceFrame = 256.0f;
	[[nodiscard]] BakeBuffers BuildDistanceBake(const MeshData& a_mesh, const Vec3& a_from);

	[[nodiscard]] BakeBuffers BuildUvBake(const MeshData& a_mesh, UvAxis a_axis);
}
