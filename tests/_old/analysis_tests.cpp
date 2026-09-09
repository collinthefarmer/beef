#include "Analysis.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;

namespace
{
	MeshVertex At(float a_x, float a_y, float a_z, float a_u, float a_v)
	{
		MeshVertex vertex;
		vertex.position = Vec3{ a_x, a_y, a_z };
		vertex.uv = Vec2{ a_u, a_v };
		return vertex;
	}

	MeshVertex Skinned(MeshVertex a_vertex, std::uint16_t a_bone0, float a_weight0, std::uint16_t a_bone1, float a_weight1)
	{
		a_vertex.bones = { a_bone0, a_bone1, 0, 0 };
		a_vertex.weights = { a_weight0, a_weight1, 0.0f, 0.0f };
		return a_vertex;
	}

	MeshData TwoPieces()
	{
		MeshData      mesh;
		MeshPartition body;
		body.slot = 32;
		body.boneNames = { "NPC Spine2 [Spn2]", "NPC Pelvis [Pelv]" };
		body.vertices = {
			Skinned(At(0, 0, 0, 0.0f, 0.0f), 0, 0.75f, 1, 0.25f),
			Skinned(At(2, 0, 0, 0.1f, 0.0f), 0, 0.75f, 1, 0.25f),
			Skinned(At(2, 0, 2, 0.1f, 0.1f), 0, 0.75f, 1, 0.25f),
			Skinned(At(0, 0, 2, 0.0f, 0.1f), 0, 0.75f, 1, 0.25f),
		};
		body.triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
		mesh.partitions.push_back(body);
		MeshPartition hand;
		hand.slot = 33;
		hand.boneNames = { "NPC R Hand [RHnd]" };
		hand.vertices = {
			Skinned(At(10, 10, 10, 0.5f, 0.5f), 0, 1.0f, 0, 0.0f),
			Skinned(At(12, 10, 10, 0.6f, 0.5f), 0, 1.0f, 0, 0.0f),
			Skinned(At(11, 10, 13, 0.55f, 0.6f), 0, 1.0f, 0, 0.0f),
		};
		hand.triangles = { { 0, 1, 2 } };
		mesh.partitions.push_back(hand);
		return mesh;
	}

	MeshData SeamMesh()
	{
		MeshData      mesh;
		MeshPartition part;
		part.vertices = {
			At(0, 0, 0, 0.0f, 0.0f),
			At(1, 0, 0, 0.1f, 0.0f),
			At(1, 0, 1, 0.1f, 0.1f),
			At(0, 0, 0, 0.5f, 0.5f),
			At(1, 0, 1, 0.6f, 0.6f),
			At(0, 0, 1, 0.5f, 0.6f),
		};
		part.triangles = { { 0, 1, 2 }, { 3, 4, 5 } };
		mesh.partitions.push_back(part);
		return mesh;
	}

	MeshData ManyTinyPieces(std::size_t a_count)
	{
		MeshData      mesh;
		MeshPartition part;
		for (std::size_t i = 0; i < a_count; ++i) {
			const float         x = static_cast<float>(i) * 10.0f;
			const float         u = static_cast<float>(i) / static_cast<float>(a_count);
			const std::uint32_t base = static_cast<std::uint32_t>(part.vertices.size());
			part.vertices.push_back(At(x, 0, 0, u, 0.0f));
			part.vertices.push_back(At(x + 1, 0, 0, u + 0.0005f, 0.0f));
			part.vertices.push_back(At(x, 0, 1, u, 0.5f));
			part.triangles.push_back({ base, base + 1, base + 2 });
		}
		mesh.partitions.push_back(part);
		return mesh;
	}

	const MeshIsland* RegionAt(const MeshAnalysis& a_analysis, IslandSource a_source, std::uint16_t a_id)
	{
		for (const MeshIsland& region : a_analysis.islands) {
			if (region.source == a_source && region.id == a_id) {
				return &region;
			}
		}
		return nullptr;
	}

	void Components()
	{
		using namespace test;
		const MeshAnalysis analysis = AnalyseMesh(TwoPieces());
		Check(analysis.components == 2, "two separate pieces are two components");
		Check(analysis.componentOf == std::vector<std::uint16_t>{ 0, 0, 0, 0, 1, 1, 1 }, "the component table runs over both partitions, largest first");
		const MeshIsland* quad = RegionAt(analysis, IslandSource::kComponent, 0);
		const MeshIsland* tri = RegionAt(analysis, IslandSource::kComponent, 1);
		Check(quad && tri, "both components are listed");
		if (quad && tri) {
			Check(quad->triangles == 2 && Near(quad->share, 2.0f / 3.0f), "the quad holds two of three triangles");
			Check(tri->triangles == 1 && Near(tri->share, 1.0f / 3.0f), "the triangle holds one of three");
			Check(quad->dominantBone == "NPC Spine2 [Spn2]" && Near(quad->dominantShare, 0.75f), "the quad's dominant bone is the spine at its summed share");
			Check(tri->dominantBone == "NPC R Hand [RHnd]" && Near(tri->dominantShare, 1.0f), "the triangle's dominant bone is the hand, wholly");
			Check(Near(quad->centroid.x, 1.0f) && Near(quad->centroid.y, 0.0f) && Near(quad->centroid.z, 1.0f), "the quad's centroid is its mean position in mesh units");
			Check(Near(tri->centroid.x, 11.0f) && Near(tri->centroid.y, 10.0f) && Near(tri->centroid.z, 11.0f), "the triangle's centroid is its mean position");
		}
		Check(analysis.charts == 2 && analysis.chartOf == std::vector<std::uint16_t>{ 0, 0, 0, 0, 1, 1, 1 }, "pieces apart in UV are two charts");
		Check(analysis.islands.size() == 4 && analysis.islands[0].source == IslandSource::kComponent && analysis.islands[2].source == IslandSource::kChart, "components list first, then charts");
		Check(quad && quad->twin == 0 && tri && tri->twin == 1 && analysis.islands[2].twin == 0 && analysis.islands[3].twin == 1, "a chart with a component's vertices is its twin, both ways");
		Check(AnalyseMesh(TwoPieces()) == analysis, "the same mesh analyses the same way twice");
	}

	void Charts()
	{
		using namespace test;
		const MeshAnalysis analysis = AnalyseMesh(SeamMesh());
		Check(analysis.components == 1, "vertices at one position weld into one component across a seam");
		Check(analysis.componentOf == std::vector<std::uint16_t>{ 0, 0, 0, 0, 0, 0 }, "every seam vertex is in the one component");
		Check(analysis.charts == 2, "the two UV islands are two charts");
		Check(analysis.chartOf == std::vector<std::uint16_t>{ 0, 0, 0, 1, 1, 1 }, "the chart table splits at the seam");
		const MeshIsland* component = RegionAt(analysis, IslandSource::kComponent, 0);
		Check(component && component->dominantBone.empty() && Near(component->dominantShare, 0.0f), "an unskinned mesh has no dominant bone");
		Check(component && Near(component->share, 1.0f), "the one component holds every triangle");
		Check(component && !component->twin && std::ranges::none_of(analysis.islands, [](const MeshIsland& i) { return i.twin.has_value(); }), "a component split into two charts has no twin, nor do the charts");
		MeshData unreached = SeamMesh();
		unreached.partitions[0].vertices.push_back(At(5, 5, 5, 0.9f, 0.9f));
		const MeshAnalysis withStray = AnalyseMesh(unreached);
		Check(withStray.componentOf.size() == 7 && withStray.componentOf[6] == kNoIsland && withStray.chartOf[6] == kNoIsland, "a vertex no triangle reaches is kNoIsland in both tables");
		MeshData welded = SeamMesh();
		welded.partitions[0].vertices[3].uv = Vec2{ 0.0f, 0.0f + kChartWeldUv * 0.25f };
		welded.partitions[0].vertices[4].uv = Vec2{ 0.1f, 0.1f };
		Check(AnalyseMesh(welded).charts == 1, "UVs within the weld join one chart");
		MeshData outOfRange = SeamMesh();
		outOfRange.partitions[0].triangles.push_back({ 0, 1, 99 });
		Check(AnalyseMesh(outOfRange) == analysis, "a triangle past its partition is skipped");
	}

	void Caps()
	{
		using namespace test;
		const std::size_t  count = kMaxIslands + 45;
		const MeshAnalysis analysis = AnalyseMesh(ManyTinyPieces(count));
		Check(analysis.components == kMaxIslands, "components list at most kMaxIslands");
		Check(analysis.charts == kMaxIslands, "charts list at most kMaxIslands");
		Check(analysis.islands.size() == 2 * static_cast<std::size_t>(kMaxIslands), "the region list holds the cap of each source");
		Check(analysis.componentOf.size() == count * 3, "the table has one entry per vertex");
		Check(analysis.componentOf[0] == 0 && analysis.componentOf[(kMaxIslands - 1) * 3] == kMaxIslands - 1, "the listed components keep dense ids");
		Check(analysis.componentOf[kMaxIslands * 3] == kNoIsland && analysis.componentOf.back() == kNoIsland, "components past the cap are kNoIsland");
		Check(analysis.islands.front().id == 0 && analysis.islands[kMaxIslands - 1].id == kMaxIslands - 1, "ids are dense from 0");
		Check(AnalyseMesh(MeshData{}).islands.empty() && AnalyseMesh(MeshData{}).componentOf.empty(), "an empty mesh has no regions and empty tables");
	}

	void RegionBakes()
	{
		using namespace test;
		const MeshData     mesh = TwoPieces();
		const MeshAnalysis analysis = AnalyseMesh(mesh);
		const BakeBuffers  bake = BuildIslandBake(mesh, analysis, IslandSource::kComponent);
		Check(bake.problem.empty() && !bake.vector, "a component bake is a scalar with no problem");
		Check(bake.vertices.size() == 7 && bake.indices.size() == 9, "every triangle of every partition bakes");
		if (bake.vertices.size() == 7) {
			Check(Near(bake.vertices[0].value[0], 0.0f), "component 0 bakes 0");
			Check(Near(bake.vertices[4].value[0], 1.0f / 255.0f), "component 1 bakes 1/255");
			Check(Near(bake.vertices[4].u, 0.5f) && Near(bake.vertices[4].v, 0.5f), "the uv rides along");
			Check(bake.indices[6] == 4 && bake.indices[8] == 6, "the second partition's indices are offset");
		}
		const BakeBuffers charts = BuildIslandBake(mesh, analysis, IslandSource::kChart);
		Check(charts.problem.empty() && Near(charts.vertices[6].value[0], 1.0f / 255.0f), "a chart bake carries the chart id");
		MeshData stray = mesh;
		stray.partitions[1].vertices.push_back(At(9, 9, 9, 0.9f, 0.9f));
		const BakeBuffers withStray = BuildIslandBake(stray, AnalyseMesh(stray), IslandSource::kComponent);
		Check(withStray.vertices.size() == 8 && Near(withStray.vertices[7].value[0], 1.0f), "a kNoIsland vertex bakes 1");
		Check(!BuildIslandBake(mesh, MeshAnalysis{}, IslandSource::kComponent).problem.empty(), "an analysis without components is a problem");
		Check(!BuildIslandBake(mesh, MeshAnalysis{}, IslandSource::kChart).problem.empty(), "an analysis without charts is a problem");
		Check(!BuildIslandBake(stray, analysis, IslandSource::kComponent).problem.empty(), "an analysis of a smaller mesh is a problem");
	}

	MaterialTexel Leather()
	{
		return MaterialTexel{ 0.9f, 0.0f, 0.8f, 0.5f, 0.1f };
	}

	MaterialTexel Steel()
	{
		return MaterialTexel{ 0.1f, 1.0f, 0.9f, 0.5f, 0.9f };
	}

	MaterialSample TwoPopulations()
	{
		MaterialSample sample;
		sample.width = 8;
		sample.height = 8;
		for (std::size_t i = 0; i < 64; ++i) {
			MaterialTexel texel = i % 4 == 0 ? Steel() : Leather();
			texel.roughness += static_cast<float>(i % 3) * 0.01f;
			texel.luma += static_cast<float>(i % 5) * 0.01f;
			sample.texels.push_back(texel);
		}
		return sample;
	}

	MaterialSample Distinct(std::size_t a_count)
	{
		MaterialSample sample;
		sample.width = static_cast<std::uint32_t>(a_count);
		sample.height = 1;
		for (std::size_t i = 0; i < a_count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_count);
			sample.texels.push_back(MaterialTexel{ t, 1.0f - t, t, 0.5f, t });
		}
		return sample;
	}

	void Clustering()
	{
		using namespace test;
		ClusterSettings settings;
		settings.clusters = 2;
		const MaterialAnalysis analysis = ClusterMaterial(TwoPopulations(), settings);
		Check(analysis.clusters.size() == 2, "two populations cluster into two");
		if (analysis.clusters.size() == 2) {
			Check(analysis.clusters[0].id == 0 && analysis.clusters[1].id == 1, "cluster ids are dense from 0");
			Check(Near(analysis.clusters[0].share, 0.75f) && Near(analysis.clusters[1].share, 0.25f), "clusters list in descending share");
			Check(analysis.clusters[0].description == "rough dark non-metal", "the larger cluster is the leather");
			Check(analysis.clusters[1].description == "polished bright metal", "the smaller cluster is the steel");
			Check(analysis.clusters[0].centroid.metallic < 0.1f && analysis.clusters[1].centroid.metallic > 0.9f, "centroids sit inside their populations");
			Check(NearestCluster(Leather(), analysis) == 0 && NearestCluster(Steel(), analysis) == 1, "NearestCluster agrees with the assignment");
			bool agree = true;
			for (const MaterialTexel& texel : TwoPopulations().texels) {
				agree = agree && NearestCluster(texel, analysis) == (texel.metallic > 0.5f ? 1 : 0);
			}
			Check(agree, "every sampled texel is nearest the cluster it was assigned");
		}
		Check(ClusterMaterial(TwoPopulations(), settings) == analysis, "the same sample and settings cluster the same way twice");
		Check(analysis.settings == settings, "the analysis carries its settings");
		ClusterSettings otherSeed = settings;
		otherSeed.seed = 77;
		const MaterialAnalysis reseeded = ClusterMaterial(TwoPopulations(), otherSeed);
		Check(reseeded.clusters.size() == 2 && Near(reseeded.clusters[0].share, 0.75f), "another seed finds the same populations");
		ClusterSettings noMetal = settings;
		noMetal.weights = ChannelWeights{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
		MaterialSample metalOnly;
		metalOnly.width = 4;
		metalOnly.height = 1;
		metalOnly.texels = { MaterialTexel{ 0.5f, 0.0f, 0.5f, 0.5f, 0.5f }, MaterialTexel{ 0.5f, 1.0f, 0.5f, 0.5f, 0.5f }, MaterialTexel{ 0.5f, 0.0f, 0.5f, 0.5f, 0.5f }, MaterialTexel{ 0.5f, 1.0f, 0.5f, 0.5f, 0.5f } };
		Check(ClusterMaterial(metalOnly, noMetal).clusters.size() == 2, "a weighted channel separates texels");
		noMetal.weights = ChannelWeights{ 1.0f, 0.0f, 1.0f, 1.0f, 1.0f };
		Check(ClusterMaterial(metalOnly, noMetal).clusters.size() == 1, "a channel of weight 0 does not separate texels");
		ClusterSettings noPasses = settings;
		noPasses.iterations = 0;
		Check(ClusterMaterial(TwoPopulations(), noPasses).clusters.size() == 2, "zero iterations still assigns the seeds");
	}

	void ClusterBounds()
	{
		using namespace test;
		ClusterSettings settings;
		Check(ClusterMaterial(MaterialSample{}, settings).clusters.empty(), "an empty sample has no clusters");
		settings.clusters = 200;
		Check(ClusterMaterial(Distinct(20), settings).clusters.size() == kMaxClusters, "clusters clamp to the cap");
		settings.clusters = 8;
		Check(ClusterMaterial(Distinct(3), settings).clusters.size() == 3, "clusters clamp to the texel count");
		settings.clusters = 0;
		Check(ClusterMaterial(Distinct(20), settings).clusters.size() == 1, "zero clusters asks for one");
		settings.clusters = 4;
		MaterialSample same;
		same.width = 8;
		same.height = 8;
		same.texels.assign(64, Leather());
		const MaterialAnalysis identical = ClusterMaterial(same, settings);
		Check(identical.clusters.size() == 1 && Near(identical.clusters[0].share, 1.0f), "identical texels are one cluster of everything");
		MaterialSample one;
		one.width = 1;
		one.height = 1;
		one.texels = { Steel() };
		const MaterialAnalysis single = ClusterMaterial(one, settings);
		Check(single.clusters.size() == 1 && single.clusters[0].description == "polished bright metal", "one texel is one cluster");
		MaterialSample garbage = TwoPopulations();
		garbage.texels[0] = MaterialTexel{ -5.0f, 40.0f, 0.5f, 0.5f, 0.5f };
		garbage.texels[1].luma = 1.0f / 0.0f;
		garbage.texels[2].roughness = 0.0f / 0.0f;
		const MaterialAnalysis sanitised = ClusterMaterial(garbage, settings);
		Check(!sanitised.clusters.empty() && sanitised.clusters[0].share <= 1.0f, "texels out of range or not numbers still cluster");
		settings.weights = ChannelWeights{ 0.0f / 0.0f, -1.0f, 0.0f, 0.0f, 0.0f };
		Check(ClusterMaterial(TwoPopulations(), settings).clusters.size() == 1, "weights that are negative or not numbers count as zero");
		MaterialSample oversized;
		oversized.width = 100;
		oversized.height = 100;
		oversized.texels.assign(kMaxSampleTexels + 500, Leather());
		Check(ClusterMaterial(oversized, ClusterSettings{}).clusters.size() == 1, "a sample past the cap clusters over the cap's texels");
		Check(NearestCluster(Leather(), MaterialAnalysis{}) == 0, "the nearest cluster of none is 0");
	}

	void Bands()
	{
		using namespace test;
		Check(DescribeTexel(MaterialTexel{ 0.9f, 0.0f, 0.5f, 0.5f, 0.1f }) == "rough dark non-metal", "rough dark non-metal");
		Check(DescribeTexel(MaterialTexel{ 0.1f, 1.0f, 0.5f, 0.5f, 0.9f }) == "polished bright metal", "polished bright metal");
		Check(DescribeTexel(MaterialTexel{ 0.5f, 0.5f, 0.5f, 0.5f, 0.5f }) == "matte mid non-metal", "the middle of every band, with metallic at the edge non-metal");
		Check(DescribeTexel(MaterialTexel{ 0.35f, 0.51f, 0.5f, 0.5f, 0.35f }) == "matte mid metal", "the lower edges of the middle bands");
		Check(DescribeTexel(MaterialTexel{ 0.65f, 0.0f, 0.5f, 0.5f, 0.65f }) == "rough bright non-metal", "the upper edges of the middle bands");
		Check(DescribeTexel(MaterialTexel{ 0.0f / 0.0f, 2.0f, 0.5f, 0.5f, -1.0f }) == "polished dark metal", "a texel out of range describes as clamped");
		Check(IslandSourceName(IslandSource::kComponent) == "component" && IslandSourceName(IslandSource::kChart) == "chart", "region sources have names");
		Check(PlainIslandSourceName(IslandSource::kComponent) == "part" && PlainIslandSourceName(IslandSource::kChart) == "chart", "region sources have plain names for labels");
		Check(IslandSourceName(static_cast<IslandSource>(9)) == "?" && PlainIslandSourceName(static_cast<IslandSource>(9)) == "?", "an unknown source names '?'");
	}
}

namespace
{
	void Spellings()
	{
		ClusterSettings settings;
		settings.clusters = 3;
		settings.weights.luma = 2.0f;
		settings.seed = 7;
		const auto source = SourceOf(settings);
		test::Check(source.clusters == 3 && source.luma == 2.0f && source.seed == 7 && source.roughness == 1.0f, "settings spell as the file's source");
		test::Check(SettingsOf(source) == settings, "and read back as the same settings");
	}
}

int main()
{
	Components();
	Charts();
	Caps();
	RegionBakes();
	Clustering();
	ClusterBounds();
	Bands();
	Spellings();
	return test::Finish("analysis");
}
