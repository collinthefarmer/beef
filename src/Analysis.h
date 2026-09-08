#pragma once

#include "Core.h"
#include "Mesh.h"
#include "Recipe.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR
{

	enum class IslandSource
	{
		kComponent,
		kChart,
	};
	struct IslandSourceSpec
	{
		IslandSource     value;
		std::string_view name;
		std::string_view plainName;
		BakeKind         bake;
	};
	inline constexpr IslandSourceSpec kIslandSources[]{
		{ IslandSource::kComponent, "component", "part", ComponentIdBake{} },
		{ IslandSource::kChart, "chart", "chart", ChartIdBake{} },
	};
	[[nodiscard]] std::string_view IslandSourceName(IslandSource a_source) noexcept;
	[[nodiscard]] std::string_view PlainIslandSourceName(IslandSource a_source) noexcept;
	[[nodiscard]] SourceKind IslandBakeOf(IslandSource a_source) noexcept;

	inline constexpr std::uint16_t kNoIsland = 0xFFFF;
	inline constexpr std::uint16_t kMaxIslands = 255;

	struct MeshIsland
	{
		IslandSource       source = IslandSource::kComponent;
		std::uint16_t      id = 0;
		std::size_t        triangles = 0;
		float              share = 0.0f;
		std::string        dominantBone;
		float              dominantShare = 0.0f;
		Vec3               centroid;
		std::optional<std::uint16_t> twin;
		[[nodiscard]] bool           operator==(const MeshIsland&) const = default;
	};

	struct MeshAnalysis
	{
		std::vector<MeshIsland>    islands;
		std::vector<std::uint16_t> componentOf;
		std::vector<std::uint16_t> chartOf;
		std::uint16_t              components = 0;
		std::uint16_t              charts = 0;
		[[nodiscard]] bool         operator==(const MeshAnalysis&) const = default;
	};

	inline constexpr float kChartWeldUv = 1.0f / 4096.0f;

	[[nodiscard]] MeshAnalysis AnalyseMesh(const MeshData& a_mesh);

	[[nodiscard]] BakeBuffers BuildIslandBake(const MeshData& a_mesh, const MeshAnalysis& a_analysis, IslandSource a_source);

	struct MaterialTexel
	{
		float              roughness = 0.0f;
		float              metallic = 0.0f;
		float              occlusion = 0.0f;
		float              reflectance = 0.0f;
		float              luma = 0.0f;
		[[nodiscard]] bool operator==(const MaterialTexel&) const = default;
	};

	inline constexpr std::size_t kMaxSampleTexels = 64 * 64;
	struct MaterialSample
	{
		std::uint32_t      width = 0;
		std::uint32_t      height = 0;
		std::vector<MaterialTexel> texels;
		[[nodiscard]] bool operator==(const MaterialSample&) const = default;
	};

	struct ChannelWeights
	{
		float              roughness = 1.0f;
		float              metallic = 1.0f;
		float              occlusion = 0.5f;
		float              reflectance = 0.5f;
		float              luma = 1.0f;
		[[nodiscard]] bool operator==(const ChannelWeights&) const = default;
	};

	inline constexpr std::uint8_t kMaxClusters = 8;
	struct ClusterSettings
	{
		std::uint8_t       clusters = 4;
		ChannelWeights     weights;
		std::uint32_t      seed = 1;
		std::uint32_t      iterations = 32;
		[[nodiscard]] bool operator==(const ClusterSettings&) const = default;
	};

	struct MaterialCluster
	{
		std::uint8_t       id = 0;
		MaterialTexel              centroid;
		float              share = 0.0f;
		std::string        description;
		[[nodiscard]] bool operator==(const MaterialCluster&) const = default;
	};

	struct MaterialAnalysis
	{
		ClusterSettings              settings;
		std::vector<MaterialCluster> clusters;
		[[nodiscard]] bool           operator==(const MaterialAnalysis&) const = default;
	};

	[[nodiscard]] ClusterSettings        SettingsOf(const MaterialClustersSource& a_source) noexcept;
	[[nodiscard]] MaterialClustersSource SourceOf(const ClusterSettings& a_settings) noexcept;

	[[nodiscard]] MaterialAnalysis ClusterMaterial(const MaterialSample& a_sample, const ClusterSettings& a_settings);
	[[nodiscard]] std::uint8_t NearestCluster(const MaterialTexel& a_texel, const MaterialAnalysis& a_analysis) noexcept;
	[[nodiscard]] std::string DescribeTexel(const MaterialTexel& a_texel);
}
