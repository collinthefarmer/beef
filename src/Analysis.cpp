#include "Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>

namespace WornEnchantmentPBR
{
	std::string_view IslandSourceName(IslandSource a_source) noexcept
	{
		return NameOf(kIslandSources, a_source);
	}

	std::string_view PlainIslandSourceName(IslandSource a_source) noexcept
	{
		const auto* row = RowOf(kIslandSources, a_source);
		return row ? row->plainName : "?";
	}

	SourceKind IslandBakeOf(IslandSource a_source) noexcept
	{
		const auto* row = RowOf(kIslandSources, a_source);
		return SourceKind{ BakeSource{ row ? row->bake : BakeKind{ ComponentIdBake{} } } };
	}

	namespace
	{

		constexpr std::size_t kMaxAnalysedVertices = std::size_t{ 1 } << 24;
		constexpr float kComponentWeldUnits = 1.0f / 1024.0f;
		constexpr float kMaxWeldCell = 1099511627776.0f;
		constexpr std::uint32_t kMaxIterations = 256;

		class DisjointSets
		{
		public:
			explicit DisjointSets(std::size_t a_count) :
				_parent(a_count), _size(a_count, 1)
			{
				std::iota(_parent.begin(), _parent.end(), std::uint32_t{ 0 });
			}

			[[nodiscard]] std::uint32_t Find(std::uint32_t a_index) noexcept
			{
				if (a_index >= _parent.size()) {
					return a_index;
				}
				while (_parent[a_index] != a_index) {
					_parent[a_index] = _parent[_parent[a_index]];
					a_index = _parent[a_index];
				}
				return a_index;
			}

			void Join(std::uint32_t a_left, std::uint32_t a_right) noexcept
			{
				std::uint32_t l = Find(a_left);
				std::uint32_t r = Find(a_right);
				if (l == r || l >= _parent.size() || r >= _parent.size()) {
					return;
				}
				if (_size[l] < _size[r]) {
					std::swap(l, r);
				}
				_parent[r] = l;
				_size[l] += _size[r];
			}

		private:
			std::vector<std::uint32_t> _parent;
			std::vector<std::uint32_t> _size;
		};

		struct FlatVertex
		{
			const MeshVertex*    vertex = nullptr;
			const MeshPartition* partition = nullptr;
		};

		struct FlatMesh
		{
			std::vector<FlatVertex>                   vertices;
			std::vector<std::array<std::uint32_t, 3>> triangles;
		};

		[[nodiscard]] std::optional<FlatMesh> Flatten(const MeshData& a_mesh)
		{
			std::size_t total = 0;
			for (const MeshPartition& partition : a_mesh.partitions) {
				total += partition.vertices.size();
			}
			if (total > kMaxAnalysedVertices) {
				return std::nullopt;
			}
			FlatMesh flat;
			flat.vertices.reserve(total);
			for (const MeshPartition& partition : a_mesh.partitions) {
				const std::uint32_t base = static_cast<std::uint32_t>(flat.vertices.size());
				const std::size_t   count = partition.vertices.size();
				for (const MeshVertex& vertex : partition.vertices) {
					flat.vertices.push_back(FlatVertex{ &vertex, &partition });
				}
				for (const std::array<std::uint32_t, 3>& triangle : partition.triangles) {
					if (triangle[0] < count && triangle[1] < count && triangle[2] < count) {
						flat.triangles.push_back({ base + triangle[0], base + triangle[1], base + triangle[2] });
					}
				}
			}
			return flat;
		}

		using WeldCell = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

		[[nodiscard]] std::optional<std::int64_t> CellOf(float a_value, float a_cellSize) noexcept
		{
			const float scaled = a_value / a_cellSize;
			if (!std::isfinite(scaled)) {
				return std::nullopt;
			}
			return static_cast<std::int64_t>(std::floor(std::clamp(scaled, -kMaxWeldCell, kMaxWeldCell)));
		}

		[[nodiscard]] std::optional<WeldCell> PositionCell(const MeshVertex& a_vertex) noexcept
		{
			const std::optional<std::int64_t> x = CellOf(a_vertex.position.x, kComponentWeldUnits);
			const std::optional<std::int64_t> y = CellOf(a_vertex.position.y, kComponentWeldUnits);
			const std::optional<std::int64_t> z = CellOf(a_vertex.position.z, kComponentWeldUnits);
			if (!x || !y || !z) {
				return std::nullopt;
			}
			return WeldCell{ *x, *y, *z };
		}

		[[nodiscard]] std::optional<WeldCell> UvCell(const MeshVertex& a_vertex) noexcept
		{
			const std::optional<std::int64_t> u = CellOf(a_vertex.uv.x, kChartWeldUv);
			const std::optional<std::int64_t> v = CellOf(a_vertex.uv.y, kChartWeldUv);
			if (!u || !v) {
				return std::nullopt;
			}
			return WeldCell{ *u, *v, 0 };
		}

		using CellOfVertex = std::optional<WeldCell> (*)(const MeshVertex&) noexcept;

		[[nodiscard]] DisjointSets Connect(const FlatMesh& a_flat, CellOfVertex a_cellOf)
		{
			DisjointSets                       sets(a_flat.vertices.size());
			std::map<WeldCell, std::uint32_t>  firstInCell;
			for (const std::array<std::uint32_t, 3>& triangle : a_flat.triangles) {
				sets.Join(triangle[0], triangle[1]);
				sets.Join(triangle[1], triangle[2]);
				for (const std::uint32_t corner : triangle) {
					if (corner >= a_flat.vertices.size() || !a_flat.vertices[corner].vertex) {
						continue;
					}
					const std::optional<WeldCell> cell = a_cellOf(*a_flat.vertices[corner].vertex);
					if (!cell) {
						continue;
					}
					const auto [it, inserted] = firstInCell.emplace(*cell, corner);
					if (!inserted) {
						sets.Join(corner, it->second);
					}
				}
			}
			return sets;
		}

		struct Labelling
		{
			std::vector<std::uint16_t> ofVertex;
			std::vector<MeshIsland>    islands;
		};

		struct RootCount
		{
			std::uint32_t root = 0;
			std::size_t   triangles = 0;
		};

		[[nodiscard]] Labelling Label(const FlatMesh& a_flat, DisjointSets& a_sets, IslandSource a_source)
		{
			const std::size_t        vertexCount = a_flat.vertices.size();
			std::vector<std::size_t> trianglesOfRoot(vertexCount, 0);
			for (const std::array<std::uint32_t, 3>& triangle : a_flat.triangles) {
				const std::uint32_t root = a_sets.Find(triangle[0]);
				if (root < vertexCount) {
					++trianglesOfRoot[root];
				}
			}
			std::vector<RootCount> ranked;
			for (std::uint32_t root = 0; root < vertexCount; ++root) {
				if (trianglesOfRoot[root] > 0) {
					ranked.push_back(RootCount{ root, trianglesOfRoot[root] });
				}
			}
			std::stable_sort(ranked.begin(), ranked.end(), [](const RootCount& a, const RootCount& b) {
				return a.triangles != b.triangles ? a.triangles > b.triangles : a.root < b.root;
			});
			const std::size_t listed = std::min<std::size_t>(ranked.size(), kMaxIslands);

			std::vector<std::uint16_t> idOfRoot(vertexCount, kNoIsland);
			for (std::size_t i = 0; i < listed; ++i) {
				idOfRoot[ranked[i].root] = static_cast<std::uint16_t>(i);
			}
			Labelling out;
			out.ofVertex.resize(vertexCount, kNoIsland);
			for (std::uint32_t v = 0; v < vertexCount; ++v) {
				const std::uint32_t root = a_sets.Find(v);
				out.ofVertex[v] = root < vertexCount ? idOfRoot[root] : kNoIsland;
			}

			const std::size_t                        totalTriangles = a_flat.triangles.size();
			std::vector<std::size_t>                 members(listed, 0);
			std::vector<Vec3>                        positionSum(listed);
			std::vector<std::map<std::string, float>> boneWeight(listed);
			for (std::uint32_t v = 0; v < vertexCount; ++v) {
				const std::uint16_t id = out.ofVertex[v];
				const FlatVertex&   flat = a_flat.vertices[v];
				if (id >= listed || !flat.vertex || !flat.partition) {
					continue;
				}
				++members[id];
				positionSum[id].x += flat.vertex->position.x;
				positionSum[id].y += flat.vertex->position.y;
				positionSum[id].z += flat.vertex->position.z;
				for (std::size_t slot = 0; slot < flat.vertex->bones.size(); ++slot) {
					const std::uint16_t bone = flat.vertex->bones[slot];
					const float         weight = flat.vertex->weights[slot];
					if (bone < flat.partition->boneNames.size() && std::isfinite(weight) && weight > 0.0f) {
						boneWeight[id][flat.partition->boneNames[bone]] += weight;
					}
				}
			}
			for (std::size_t i = 0; i < listed; ++i) {
				MeshIsland island;
				island.source = a_source;
				island.id = static_cast<std::uint16_t>(i);
				island.triangles = ranked[i].triangles;
				island.share = totalTriangles > 0 ? static_cast<float>(ranked[i].triangles) / static_cast<float>(totalTriangles) : 0.0f;
				if (members[i] > 0) {
					const float count = static_cast<float>(members[i]);
					island.centroid = Vec3{ positionSum[i].x / count, positionSum[i].y / count, positionSum[i].z / count };
				}
				float total = 0.0f;
				float best = 0.0f;
				for (const auto& [name, weight] : boneWeight[i]) {
					total += weight;
					if (weight > best) {
						best = weight;
						island.dominantBone = name;
					}
				}
				island.dominantShare = total > 0.0f ? best / total : 0.0f;
				out.islands.push_back(std::move(island));
			}
			return out;
		}

		[[nodiscard]] std::size_t RegionsOf(const MeshAnalysis& a_analysis, IslandSource a_source) noexcept
		{
			return a_source == IslandSource::kComponent ? a_analysis.components : a_analysis.charts;
		}

		[[nodiscard]] const std::vector<std::uint16_t>& TableOf(const MeshAnalysis& a_analysis, IslandSource a_source) noexcept
		{
			return a_source == IslandSource::kComponent ? a_analysis.componentOf : a_analysis.chartOf;
		}
	}

	namespace
	{
		void PairTwins(MeshAnalysis& a_analysis)
		{
			constexpr std::uint16_t kConflict = 0xFFFF;
			std::vector<std::uint16_t> chartOfComponent(a_analysis.components, kNoIsland);
			std::vector<std::size_t>   componentSize(a_analysis.components, 0);
			std::vector<std::size_t>   chartSize(a_analysis.charts, 0);
			const std::size_t          vertices = (std::min)(a_analysis.componentOf.size(), a_analysis.chartOf.size());
			for (std::size_t v = 0; v < vertices; ++v) {
				const std::uint16_t c = a_analysis.componentOf[v];
				const std::uint16_t k = a_analysis.chartOf[v];
				if (c < componentSize.size()) {
					++componentSize[c];
				}
				if (k < chartSize.size()) {
					++chartSize[k];
				}
				if (c >= chartOfComponent.size() || k == kNoIsland) {
					continue;
				}
				auto& seen = chartOfComponent[c];
				seen = seen == kNoIsland ? k : (seen == k ? k : kConflict);
			}
			for (auto& island : a_analysis.islands) {
				if (island.source != IslandSource::kComponent || island.id >= chartOfComponent.size()) {
					continue;
				}
				const std::uint16_t k = chartOfComponent[island.id];
				if (k == kNoIsland || k == kConflict || k >= chartSize.size() || chartSize[k] != componentSize[island.id]) {
					continue;
				}
				island.twin = k;
				for (auto& chart : a_analysis.islands) {
					if (chart.source == IslandSource::kChart && chart.id == k) {
						chart.twin = island.id;
					}
				}
			}
		}
	}

	MeshAnalysis AnalyseMesh(const MeshData& a_mesh)
	{
		MeshAnalysis                  out;
		const std::optional<FlatMesh> flat = Flatten(a_mesh);
		if (!flat) {
			return out;
		}
		DisjointSets components = Connect(*flat, &PositionCell);
		Labelling    byComponent = Label(*flat, components, IslandSource::kComponent);
		DisjointSets charts = Connect(*flat, &UvCell);
		Labelling    byChart = Label(*flat, charts, IslandSource::kChart);

		out.components = static_cast<std::uint16_t>(byComponent.islands.size());
		out.charts = static_cast<std::uint16_t>(byChart.islands.size());
		out.islands = std::move(byComponent.islands);
		out.islands.insert(out.islands.end(), byChart.islands.begin(), byChart.islands.end());
		out.componentOf = std::move(byComponent.ofVertex);
		out.chartOf = std::move(byChart.ofVertex);
		PairTwins(out);
		return out;
	}

	BakeBuffers BuildIslandBake(const MeshData& a_mesh, const MeshAnalysis& a_analysis, IslandSource a_source)
	{
		BakeBuffers out;
		out.vector = false;
		if (RegionsOf(a_analysis, a_source) == 0) {
			out.problem = std::format("the analysis found no {} of the mesh", IslandSourceName(a_source));
			return out;
		}
		const std::vector<std::uint16_t>& table = TableOf(a_analysis, a_source);
		std::size_t                       next = 0;
		for (const MeshPartition& partition : a_mesh.partitions) {
			if (next + partition.vertices.size() > table.size()) {
				out.vertices.clear();
				out.indices.clear();
				out.problem = "the analysis is of a different mesh";
				return out;
			}
			const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size());
			for (const MeshVertex& vertex : partition.vertices) {
				const std::uint16_t id = table[next++];
				BakeVertex          bv;
				bv.u = vertex.uv.x;
				bv.v = vertex.uv.y;
				bv.value[0] = id == kNoIsland ? 1.0f : static_cast<float>(id) / 255.0f;
				out.vertices.push_back(bv);
			}
			for (const std::array<std::uint32_t, 3>& triangle : partition.triangles) {
				if (triangle[0] < partition.vertices.size() && triangle[1] < partition.vertices.size() && triangle[2] < partition.vertices.size()) {
					out.indices.push_back(base + triangle[0]);
					out.indices.push_back(base + triangle[1]);
					out.indices.push_back(base + triangle[2]);
				}
			}
		}
		if (out.indices.empty()) {
			out.vertices.clear();
			out.problem = "the mesh has no triangles to bake";
		}
		return out;
	}

	namespace
	{
		struct TexelBand
		{
			float MaterialTexel::*                     axis;
			std::span<const float>             cuts;
			std::span<const std::string_view>  words;
			bool                               belowInclusive = false;
		};

		constexpr float            kRoughnessCuts[]{ 0.35f, 0.65f };
		constexpr std::string_view kRoughnessWords[]{ "polished", "matte", "rough" };
		static_assert(std::size(kRoughnessWords) == std::size(kRoughnessCuts) + 1);
		constexpr float            kLumaCuts[]{ 0.35f, 0.65f };
		constexpr std::string_view kLumaWords[]{ "dark", "mid", "bright" };
		static_assert(std::size(kLumaWords) == std::size(kLumaCuts) + 1);
		constexpr float            kMetallicCuts[]{ 0.5f };
		constexpr std::string_view kMetallicWords[]{ "non-metal", "metal" };
		static_assert(std::size(kMetallicWords) == std::size(kMetallicCuts) + 1);

		constexpr TexelBand kTexelBands[]{
			{ &MaterialTexel::roughness, kRoughnessCuts, kRoughnessWords, false },
			{ &MaterialTexel::luma, kLumaCuts, kLumaWords, false },
			{ &MaterialTexel::metallic, kMetallicCuts, kMetallicWords, true },
		};

		[[nodiscard]] std::string_view BandWord(const TexelBand& a_band, float a_value) noexcept
		{
			for (std::size_t i = 0; i < a_band.cuts.size(); ++i) {
				const bool below = a_band.belowInclusive ? a_value <= a_band.cuts[i] : a_value < a_band.cuts[i];
				if (below) {
					return a_band.words[i];
				}
			}
			return a_band.words.back();
		}

		constexpr std::size_t kAxes = 5;
		using Axes = std::array<float, kAxes>;

		[[nodiscard]] Axes AxesOf(const MaterialTexel& a_texel) noexcept
		{
			return Axes{ a_texel.roughness, a_texel.metallic, a_texel.occlusion, a_texel.reflectance, a_texel.luma };
		}

		[[nodiscard]] MaterialTexel TexelOf(const Axes& a_axes) noexcept
		{
			return MaterialTexel{ a_axes[0], a_axes[1], a_axes[2], a_axes[3], a_axes[4] };
		}

		[[nodiscard]] float Finite01(float a_value) noexcept
		{
			return std::isfinite(a_value) ? Clamp01(a_value) : 0.0f;
		}

		[[nodiscard]] MaterialTexel Sanitised(const MaterialTexel& a_texel) noexcept
		{
			return MaterialTexel{ Finite01(a_texel.roughness), Finite01(a_texel.metallic), Finite01(a_texel.occlusion), Finite01(a_texel.reflectance), Finite01(a_texel.luma) };
		}

		[[nodiscard]] Axes ScalesOf(const ChannelWeights& a_weights) noexcept
		{
			const Axes raw{ a_weights.roughness, a_weights.metallic, a_weights.occlusion, a_weights.reflectance, a_weights.luma };
			Axes       scales{};
			for (std::size_t i = 0; i < kAxes; ++i) {
				scales[i] = std::isfinite(raw[i]) && raw[i] > 0.0f ? raw[i] : 0.0f;
			}
			return scales;
		}

		[[nodiscard]] float Distance(const Axes& a_left, const Axes& a_right, const Axes& a_scales) noexcept
		{
			float sum = 0.0f;
			for (std::size_t i = 0; i < kAxes; ++i) {
				const float d = a_left[i] - a_right[i];
				sum += a_scales[i] * d * d;
			}
			return sum;
		}

		class Lcg
		{
		public:
			explicit Lcg(std::uint32_t a_seed) noexcept :
				_state(a_seed)
			{}

			[[nodiscard]] float Unit() noexcept
			{
				_state = _state * 1664525u + 1013904223u;
				return static_cast<float>(_state >> 8) / 16777216.0f;
			}

		private:
			std::uint32_t _state;
		};

		[[nodiscard]] std::vector<Axes> SeedCentroids(const std::vector<Axes>& a_texels, std::size_t a_count, const Axes& a_scales, Lcg& a_random)
		{
			std::vector<Axes> centroids;
			if (a_texels.empty() || a_count == 0) {
				return centroids;
			}
			const std::size_t first = std::min<std::size_t>(static_cast<std::size_t>(a_random.Unit() * static_cast<float>(a_texels.size())), a_texels.size() - 1);
			centroids.push_back(a_texels[first]);
			std::vector<float> nearest(a_texels.size(), 0.0f);
			while (centroids.size() < a_count) {
				float total = 0.0f;
				for (std::size_t i = 0; i < a_texels.size(); ++i) {
					float best = Distance(a_texels[i], centroids[0], a_scales);
					for (std::size_t c = 1; c < centroids.size(); ++c) {
						best = std::min(best, Distance(a_texels[i], centroids[c], a_scales));
					}
					nearest[i] = best;
					total += best;
				}
				if (!(total > 0.0f)) {
					break;
				}
				const float threshold = a_random.Unit() * total;
				float       running = 0.0f;
				std::size_t pick = a_texels.size();
				for (std::size_t i = 0; i < a_texels.size(); ++i) {
					if (nearest[i] <= 0.0f) {
						continue;
					}
					running += nearest[i];
					pick = i;
					if (running > threshold) {
						break;
					}
				}
				if (pick >= a_texels.size()) {
					break;
				}
				centroids.push_back(a_texels[pick]);
			}
			return centroids;
		}

		[[nodiscard]] std::size_t NearestOf(const Axes& a_texel, const std::vector<Axes>& a_centroids, const Axes& a_scales) noexcept
		{
			std::size_t nearest = 0;
			float       best = 0.0f;
			for (std::size_t c = 0; c < a_centroids.size(); ++c) {
				const float d = Distance(a_texel, a_centroids[c], a_scales);
				if (c == 0 || d < best) {
					best = d;
					nearest = c;
				}
			}
			return nearest;
		}

		bool Assign(const std::vector<Axes>& a_texels, const std::vector<Axes>& a_centroids, const Axes& a_scales, std::vector<std::size_t>& a_owner) noexcept
		{
			bool changed = false;
			for (std::size_t i = 0; i < a_texels.size() && i < a_owner.size(); ++i) {
				const std::size_t nearest = NearestOf(a_texels[i], a_centroids, a_scales);
				changed = changed || nearest != a_owner[i];
				a_owner[i] = nearest;
			}
			return changed;
		}

		void Recentre(const std::vector<Axes>& a_texels, const std::vector<std::size_t>& a_owner, std::vector<Axes>& a_centroids)
		{
			std::vector<Axes>        sums(a_centroids.size(), Axes{});
			std::vector<std::size_t> counts(a_centroids.size(), 0);
			for (std::size_t i = 0; i < a_texels.size() && i < a_owner.size(); ++i) {
				const std::size_t c = a_owner[i];
				if (c >= sums.size()) {
					continue;
				}
				for (std::size_t axis = 0; axis < kAxes; ++axis) {
					sums[c][axis] += a_texels[i][axis];
				}
				++counts[c];
			}
			for (std::size_t c = 0; c < a_centroids.size(); ++c) {
				if (counts[c] == 0) {
					continue;
				}
				for (std::size_t axis = 0; axis < kAxes; ++axis) {
					a_centroids[c][axis] = sums[c][axis] / static_cast<float>(counts[c]);
				}
			}
		}

		struct ClusterCount
		{
			std::size_t centroid = 0;
			std::size_t members = 0;
		};
	}

	MaterialAnalysis ClusterMaterial(const MaterialSample& a_sample, const ClusterSettings& a_settings)
	{
		MaterialAnalysis out;
		out.settings = a_settings;
		if (a_sample.texels.empty()) {
			return out;
		}
		std::vector<Axes> texels;
		texels.reserve(std::min(a_sample.texels.size(), kMaxSampleTexels));
		for (const MaterialTexel& texel : a_sample.texels) {
			if (texels.size() >= kMaxSampleTexels) {
				break;
			}
			texels.push_back(AxesOf(Sanitised(texel)));
		}
		const Axes        scales = ScalesOf(a_settings.weights);
		const std::size_t wanted = std::min<std::size_t>(std::clamp<std::size_t>(a_settings.clusters, 1, kMaxClusters), texels.size());
		Lcg               random(a_settings.seed);
		std::vector<Axes> centroids = SeedCentroids(texels, wanted, scales, random);
		if (centroids.empty()) {
			return out;
		}

		std::vector<std::size_t> owner(texels.size(), 0);
		Assign(texels, centroids, scales, owner);
		const std::uint32_t passes = std::min(a_settings.iterations, kMaxIterations);
		for (std::uint32_t pass = 0; pass < passes; ++pass) {
			Recentre(texels, owner, centroids);
			if (!Assign(texels, centroids, scales, owner)) {
				break;
			}
		}

		std::vector<ClusterCount> counts(centroids.size());
		for (std::size_t c = 0; c < centroids.size(); ++c) {
			counts[c].centroid = c;
		}
		for (const std::size_t c : owner) {
			if (c < counts.size()) {
				++counts[c].members;
			}
		}
		std::stable_sort(counts.begin(), counts.end(), [](const ClusterCount& a, const ClusterCount& b) {
			return a.members > b.members;
		});
		for (const ClusterCount& count : counts) {
			if (count.members == 0) {
				continue;
			}
			MaterialCluster cluster;
			cluster.id = static_cast<std::uint8_t>(out.clusters.size());
			cluster.centroid = TexelOf(centroids[count.centroid]);
			cluster.share = static_cast<float>(count.members) / static_cast<float>(texels.size());
			cluster.description = DescribeTexel(cluster.centroid);
			out.clusters.push_back(std::move(cluster));
		}
		return out;
	}

	std::uint8_t NearestCluster(const MaterialTexel& a_texel, const MaterialAnalysis& a_analysis) noexcept
	{
		const Axes   texel = AxesOf(Sanitised(a_texel));
		const Axes   scales = ScalesOf(a_analysis.settings.weights);
		std::uint8_t nearest = 0;
		float        best = 0.0f;
		bool         first = true;
		for (const MaterialCluster& cluster : a_analysis.clusters) {
			const float d = Distance(texel, AxesOf(cluster.centroid), scales);
			if (first || d < best) {
				best = d;
				nearest = cluster.id;
				first = false;
			}
		}
		return nearest;
	}

	std::string DescribeTexel(const MaterialTexel& a_texel)
	{
		const MaterialTexel texel = Sanitised(a_texel);
		std::string out;
		for (const auto& band : kTexelBands) {
			if (!out.empty()) {
				out += " ";
			}
			out += BandWord(band, texel.*band.axis);
		}
		return out;
	}

	ClusterSettings SettingsOf(const MaterialClustersSource& a_source) noexcept
	{
		ClusterSettings settings;
		settings.clusters = a_source.clusters;
		settings.weights = ChannelWeights{ a_source.roughness, a_source.metallic, a_source.occlusion, a_source.reflectance, a_source.luma };
		settings.seed = a_source.seed;
		settings.iterations = a_source.iterations;
		return settings;
	}

	MaterialClustersSource SourceOf(const ClusterSettings& a_settings) noexcept
	{
		MaterialClustersSource source;
		source.clusters = a_settings.clusters;
		source.roughness = a_settings.weights.roughness;
		source.metallic = a_settings.weights.metallic;
		source.occlusion = a_settings.weights.occlusion;
		source.reflectance = a_settings.weights.reflectance;
		source.luma = a_settings.weights.luma;
		source.seed = a_settings.seed;
		source.iterations = a_settings.iterations;
		return source;
	}
}
