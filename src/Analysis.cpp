#include "Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <optional>
#include <tuple>

namespace WornEnchantmentPBR
{
	std::string_view RegionSourceName(RegionSource a_source) noexcept
	{
		return NameOf(kRegionSources, a_source);
	}

	std::string_view PlainRegionSourceName(RegionSource a_source) noexcept
	{
		const auto* row = RowOf(kRegionSources, a_source);
		return row ? row->plainName : "?";
	}

	namespace
	{
		// ------------------------------------------------------------ bounds

		// Past this many vertices the analysis is empty rather than slow;
		// a worn piece has tens of thousands.
		constexpr std::size_t kMaxAnalysedVertices = std::size_t{ 1 } << 24;
		// Two positions closer than this are one vertex of the piece, so a
		// UV seam's duplicated vertices do not split a component.
		constexpr float kComponentWeldUnits = 1.0f / 1024.0f;
		// A weld cell index never leaves this range, so the float-to-integer
		// cast below is defined for any finite coordinate.
		constexpr float kMaxWeldCell = 1099511627776.0f;  // 2^40
		// The k-means pass cap, whatever the settings ask.
		constexpr std::uint32_t kMaxIterations = 256;

		// ---------------------------------------------------- disjoint sets

		// Union-find over the vertices: Find follows parents iteratively
		// with path halving, so no call recurses and each is bounded by
		// the tree height. An index past the set count is its own root.
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

		// --------------------------------------------------- the flat mesh

		// The partitions concatenated: one global index per vertex, and
		// every triangle rewritten to global indices inside that table.
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

		// ------------------------------------------------------- welding

		// A cell of the weld grid; two coordinates in one cell are one point.
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

		// Joins the corners of every triangle, and each corner to the first
		// corner seen in its weld cell. Only triangle corners weld, so a
		// vertex no triangle reaches stays alone whatever it coincides with.
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

		// ------------------------------------------------------ labelling

		struct Labelling
		{
			std::vector<std::uint16_t> ofVertex;
			std::vector<MeshRegion>    regions;  // descending share
		};

		struct RootCount
		{
			std::uint32_t root = 0;
			std::size_t   triangles = 0;
		};

		// Ranks the sets by triangle count, ids the largest kMaxRegions,
		// and measures each: share, dominant bone, centroid.
		[[nodiscard]] Labelling Label(const FlatMesh& a_flat, DisjointSets& a_sets, RegionSource a_source)
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
			// Ties rank by first vertex so the same mesh always labels the same way.
			std::stable_sort(ranked.begin(), ranked.end(), [](const RootCount& a, const RootCount& b) {
				return a.triangles != b.triangles ? a.triangles > b.triangles : a.root < b.root;
			});
			const std::size_t listed = std::min<std::size_t>(ranked.size(), kMaxRegions);

			std::vector<std::uint16_t> idOfRoot(vertexCount, kNoRegion);
			for (std::size_t i = 0; i < listed; ++i) {
				idOfRoot[ranked[i].root] = static_cast<std::uint16_t>(i);
			}
			Labelling out;
			out.ofVertex.resize(vertexCount, kNoRegion);
			for (std::uint32_t v = 0; v < vertexCount; ++v) {
				const std::uint32_t root = a_sets.Find(v);
				out.ofVertex[v] = root < vertexCount ? idOfRoot[root] : kNoRegion;
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
				MeshRegion region;
				region.source = a_source;
				region.id = static_cast<std::uint16_t>(i);
				region.triangles = ranked[i].triangles;
				region.share = totalTriangles > 0 ? static_cast<float>(ranked[i].triangles) / static_cast<float>(totalTriangles) : 0.0f;
				if (members[i] > 0) {
					const float count = static_cast<float>(members[i]);
					region.centroid = Vec3{ positionSum[i].x / count, positionSum[i].y / count, positionSum[i].z / count };
				}
				float total = 0.0f;
				float best = 0.0f;
				for (const auto& [name, weight] : boneWeight[i]) {
					total += weight;
					if (weight > best) {
						best = weight;
						region.dominantBone = name;
					}
				}
				region.dominantShare = total > 0.0f ? best / total : 0.0f;
				out.regions.push_back(std::move(region));
			}
			return out;
		}

		[[nodiscard]] std::size_t RegionsOf(const MeshAnalysis& a_analysis, RegionSource a_source) noexcept
		{
			return a_source == RegionSource::kComponent ? a_analysis.components : a_analysis.charts;
		}

		[[nodiscard]] const std::vector<std::uint16_t>& TableOf(const MeshAnalysis& a_analysis, RegionSource a_source) noexcept
		{
			return a_source == RegionSource::kComponent ? a_analysis.componentOf : a_analysis.chartOf;
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
		Labelling    byComponent = Label(*flat, components, RegionSource::kComponent);
		DisjointSets charts = Connect(*flat, &UvCell);
		Labelling    byChart = Label(*flat, charts, RegionSource::kChart);

		out.components = static_cast<std::uint16_t>(byComponent.regions.size());
		out.charts = static_cast<std::uint16_t>(byChart.regions.size());
		out.regions = std::move(byComponent.regions);
		out.regions.insert(out.regions.end(), byChart.regions.begin(), byChart.regions.end());
		out.componentOf = std::move(byComponent.ofVertex);
		out.chartOf = std::move(byChart.ofVertex);
		return out;
	}

	BakeBuffers BuildRegionBake(const MeshData& a_mesh, const MeshAnalysis& a_analysis, RegionSource a_source)
	{
		BakeBuffers out;
		out.vector = false;
		if (RegionsOf(a_analysis, a_source) == 0) {
			out.problem = std::format("the analysis found no {} of the mesh", RegionSourceName(a_source));
			return out;
		}
		const std::vector<std::uint16_t>& table = TableOf(a_analysis, a_source);
		std::size_t                       next = 0;  // the global index of the partition's first vertex
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
				bv.value[0] = id == kNoRegion ? 1.0f : static_cast<float>(id) / 255.0f;
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

	// --------------------------------------------------------- the material

	namespace
	{
		// The bands DescribeTexel reads; each is the value a word starts at.
		constexpr float kMetalAbove = 0.5f;
		constexpr float kPolishedBelow = 0.35f;
		constexpr float kMatteBelow = 0.65f;
		constexpr float kDarkBelow = 0.35f;
		constexpr float kMidBelow = 0.65f;

		// The five channels as axes of one space, in Texel's order.
		constexpr std::size_t kAxes = 5;
		using Axes = std::array<float, kAxes>;

		[[nodiscard]] Axes AxesOf(const Texel& a_texel) noexcept
		{
			return Axes{ a_texel.roughness, a_texel.metallic, a_texel.occlusion, a_texel.reflectance, a_texel.luma };
		}

		[[nodiscard]] Texel TexelOf(const Axes& a_axes) noexcept
		{
			return Texel{ a_axes[0], a_axes[1], a_axes[2], a_axes[3], a_axes[4] };
		}

		[[nodiscard]] float Finite01(float a_value) noexcept
		{
			return std::isfinite(a_value) ? Clamp01(a_value) : 0.0f;
		}

		[[nodiscard]] Texel Sanitised(const Texel& a_texel) noexcept
		{
			return Texel{ Finite01(a_texel.roughness), Finite01(a_texel.metallic), Finite01(a_texel.occlusion), Finite01(a_texel.reflectance), Finite01(a_texel.luma) };
		}

		// A weight below zero or not a number counts as zero.
		[[nodiscard]] Axes ScalesOf(const ChannelWeights& a_weights) noexcept
		{
			const Axes raw{ a_weights.roughness, a_weights.metallic, a_weights.occlusion, a_weights.reflectance, a_weights.luma };
			Axes       scales{};
			for (std::size_t i = 0; i < kAxes; ++i) {
				scales[i] = std::isfinite(raw[i]) && raw[i] > 0.0f ? raw[i] : 0.0f;
			}
			return scales;
		}

		// Weighted squared distance; a channel of weight 0 does not count.
		[[nodiscard]] float Distance(const Axes& a_left, const Axes& a_right, const Axes& a_scales) noexcept
		{
			float sum = 0.0f;
			for (std::size_t i = 0; i < kAxes; ++i) {
				const float d = a_left[i] - a_right[i];
				sum += a_scales[i] * d * d;
			}
			return sum;
		}

		// A linear congruential generator: the same seed gives the same
		// picks on every platform, which std::random_device would not.
		class Lcg
		{
		public:
			explicit Lcg(std::uint32_t a_seed) noexcept :
				_state(a_seed)
			{}

			// In [0, 1).
			[[nodiscard]] float Unit() noexcept
			{
				_state = _state * 1664525u + 1013904223u;
				return static_cast<float>(_state >> 8) / 16777216.0f;
			}

		private:
			std::uint32_t _state;
		};

		// k-means++ seeding: the first centroid is a random texel, each next
		// one a texel picked with probability proportional to its squared
		// distance from the nearest centroid so far. Seeding stops early
		// when every texel already sits on a centroid: a duplicate centroid
		// would only make an empty cluster.
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

		// One assignment pass; true when some texel changed cluster.
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

		// Moves each centroid to the mean of its texels; an empty cluster keeps its centroid.
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
		for (const Texel& texel : a_sample.texels) {
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

	std::uint8_t NearestCluster(const Texel& a_texel, const MaterialAnalysis& a_analysis) noexcept
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

	std::string DescribeTexel(const Texel& a_texel)
	{
		const Texel      texel = Sanitised(a_texel);
		std::string_view finish = texel.roughness < kPolishedBelow ? "polished" : (texel.roughness < kMatteBelow ? "matte" : "rough");
		std::string_view tone = texel.luma < kDarkBelow ? "dark" : (texel.luma < kMidBelow ? "mid" : "bright");
		std::string_view metal = texel.metallic > kMetalAbove ? "metal" : "non-metal";
		return std::format("{} {} {}", finish, tone, metal);
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
