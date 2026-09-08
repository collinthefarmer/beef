#pragma once

// What a piece can be shown to have, measured from its own mesh and maps:
// the mesh's connected components and UV charts with the bone that carries
// each and where it sits, and the material's clusters over its channels.
// Everything here is a pure function of a record read once (the mesh, a
// low-mip sample of the maps), deterministic on the same input, bounded
// by fixed caps, and engine-free. This module never spells the mask
// language: a region becomes a mask in Regions.

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
	// ------------------------------------------------------------ the mesh

	enum class RegionSource
	{
		kComponent,  // a connected piece of the mesh (triangles sharing vertices)
		kChart,      // a connected piece in UV space (triangles sharing UV positions)
	};
	// A source's word as the format and the log spell it, the plain word the
	// studio's labels and offers use, and the bake that reads its id map.
	struct RegionSourceRow
	{
		RegionSource     value;
		std::string_view name;
		std::string_view plainName;
		BakeKind         bake;
	};
	inline constexpr RegionSourceRow kRegionSources[]{
		{ RegionSource::kComponent, "component", "part", ComponentIdBake{} },
		{ RegionSource::kChart, "chart", "chart", ChartIdBake{} },
	};
	[[nodiscard]] std::string_view RegionSourceName(RegionSource a_source) noexcept;
	[[nodiscard]] std::string_view PlainRegionSourceName(RegionSource a_source) noexcept;
	// The id map bake of a region source, as a source's kind.
	[[nodiscard]] SourceKind RegionBakeOf(RegionSource a_source) noexcept;

	// Region ids are dense from 0 per source and fit a byte, so an id map
	// bake carries id / 255 in one channel; kNoRegion marks a vertex no
	// triangle reaches.
	inline constexpr std::uint16_t kNoRegion = 0xFFFF;
	inline constexpr std::uint16_t kMaxRegions = 255;

	struct MeshRegion
	{
		RegionSource       source = RegionSource::kComponent;
		std::uint16_t      id = 0;
		std::size_t        triangles = 0;
		float              share = 0.0f;  // of the mesh's triangles, 0..1
		std::string        dominantBone;  // the bone carrying the most summed weight; empty when unskinned
		float              dominantShare = 0.0f;  // that bone's share of the region's summed weight, 0..1
		Vec3               centroid;      // mean bind-pose position, in the frame the position bake uses
		[[nodiscard]] bool operator==(const MeshRegion&) const = default;
	};

	// Per-vertex tables run over the partitions in mesh order, concatenated.
	// Past kMaxRegions of a source, the largest regions keep their ids and
	// the rest are kNoRegion, so a bake never overflows a byte.
	struct MeshAnalysis
	{
		std::vector<MeshRegion>    regions;      // components first, then charts, each in descending share
		std::vector<std::uint16_t> componentOf;  // one per vertex
		std::vector<std::uint16_t> chartOf;      // one per vertex
		std::uint16_t              components = 0;
		std::uint16_t              charts = 0;
		[[nodiscard]] bool         operator==(const MeshAnalysis&) const = default;
	};

	// Two UV positions closer than this share a chart vertex.
	inline constexpr float kChartWeldUv = 1.0f / 4096.0f;

	[[nodiscard]] MeshAnalysis AnalyseMesh(const MeshData& a_mesh);

	// The id map of one source as a bake: each vertex carries its region id
	// as id / 255 in x (kNoRegion vertices carry 1), rasterised like any
	// bake. Empty with a problem when the analysis has no region of that source.
	[[nodiscard]] BakeBuffers BuildRegionBake(const MeshData& a_mesh, const MeshAnalysis& a_analysis, RegionSource a_source);

	// --------------------------------------------------------- the material

	// One texel of the maps that describe a material, each 0..1.
	struct Texel
	{
		float              roughness = 0.0f;
		float              metallic = 0.0f;
		float              occlusion = 0.0f;
		float              reflectance = 0.0f;
		float              luma = 0.0f;  // diffuse luminance
		[[nodiscard]] bool operator==(const Texel&) const = default;
	};

	// A low mip of the RMAOS and diffuse maps, read once per material at
	// apply; never more texels than the cap.
	inline constexpr std::size_t kMaxSampleTexels = 64 * 64;
	struct MaterialSample
	{
		std::uint32_t      width = 0;
		std::uint32_t      height = 0;
		std::vector<Texel> texels;  // row-major, width * height
		[[nodiscard]] bool operator==(const MaterialSample&) const = default;
	};

	// How much each channel counts in the distance between texels.
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
		std::uint8_t       clusters = 4;  // 1..kMaxClusters; clamped
		ChannelWeights     weights;
		std::uint32_t      seed = 1;         // the same seed and sample give the same clusters
		std::uint32_t      iterations = 32;  // the k-means cap
		[[nodiscard]] bool operator==(const ClusterSettings&) const = default;
	};

	struct MaterialCluster
	{
		std::uint8_t       id = 0;
		Texel              centroid;
		float              share = 0.0f;  // of the sample's texels
		std::string        description;   // DescribeTexel of the centroid
		[[nodiscard]] bool operator==(const MaterialCluster&) const = default;
	};

	struct MaterialAnalysis
	{
		ClusterSettings              settings;
		std::vector<MaterialCluster> clusters;  // in descending share; empty for an empty sample
		[[nodiscard]] bool           operator==(const MaterialAnalysis&) const = default;
	};

	// The file's spelling of the settings (Recipe.h's MaterialClustersSource
	// carries the same fields flat, since Recipe cannot include this header)
	// and back; the two are the one idea.
	[[nodiscard]] ClusterSettings        SettingsOf(const MaterialClustersSource& a_source) noexcept;
	[[nodiscard]] MaterialClustersSource SourceOf(const ClusterSettings& a_settings) noexcept;

	[[nodiscard]] MaterialAnalysis ClusterMaterial(const MaterialSample& a_sample, const ClusterSettings& a_settings);
	// The cluster a texel falls in under the analysis' weights; 0 when there are none.
	[[nodiscard]] std::uint8_t NearestCluster(const Texel& a_texel, const MaterialAnalysis& a_analysis) noexcept;
	// Words for a texel from fixed bands: "rough dark non-metal", "polished bright metal".
	[[nodiscard]] std::string DescribeTexel(const Texel& a_texel);
}
