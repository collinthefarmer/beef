#include "mesh/Islands.h"
#include "test_support.h"

#include <array>
#include <cstddef>
#include <cstdint>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace
{
	MeshVertex Vert(float a_px, float a_py, float a_pz, float a_u, float a_v, std::uint16_t a_bone, float a_weight)
	{
		MeshVertex vertex;
		vertex.position = Vec3{ a_px, a_py, a_pz };
		vertex.uv = Vec2{ a_u, a_v };
		vertex.bones = { a_bone, 0, 0, 0 };
		vertex.weights = { a_weight, 0.0f, 0.0f, 0.0f };
		return vertex;
	}

	MeshData TwoParts()
	{
		MeshPartition partition;
		partition.boneNames = { "root", "tip" };
		partition.vertices = {
			Vert(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.0f),
			Vert(1.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0, 1.0f),
			Vert(0.0f, 1.0f, 0.0f, 0.0f, 0.1f, 0, 1.0f),
			Vert(50.0f, 0.0f, 0.0f, 0.5f, 0.5f, 1, 1.0f),
			Vert(51.0f, 0.0f, 0.0f, 0.6f, 0.5f, 1, 1.0f),
			Vert(50.0f, 1.0f, 0.0f, 0.5f, 0.6f, 1, 1.0f),
		};
		partition.triangles = { { 0, 1, 2 }, { 3, 4, 5 } };
		MeshData mesh;
		mesh.partitions = { partition };
		return mesh;
	}

	MeshData Seam()
	{
		MeshPartition partition;
		partition.vertices = {
			Vert(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f),
			Vert(1.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0, 0.0f),
			Vert(1.0f, 1.0f, 0.0f, 0.1f, 0.1f, 0, 0.0f),
			Vert(1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0, 0.0f),
			Vert(1.0f, 0.0f, 0.0f, 0.5f, 0.4f, 0, 0.0f),
			Vert(2.0f, 1.0f, 0.0f, 0.6f, 0.5f, 0, 0.0f),
		};
		partition.triangles = { { 0, 1, 2 }, { 3, 4, 5 } };
		MeshData mesh;
		mesh.partitions = { partition };
		return mesh;
	}

	const MeshIsland* ComponentWithBone(const MeshAnalysis& a_analysis, std::string_view a_bone)
	{
		for (const MeshIsland& island : a_analysis.islands) {
			if (island.source == IslandSource::kComponent && island.dominantBone == a_bone) {
				return &island;
			}
		}
		return nullptr;
	}
}

int main()
{
	Check(kNoIsland == 0xFFFF, "the no-island sentinel is exposed");
	Check(kMaxIslands == 255, "at most 255 islands fit a byte bake");
	Check(test::Near(kChartWeldUv, 1.0f / 4096.0f), "the chart weld is one texel at 4096");

	Check(std::size(kIslandSources) == 2, "component and chart are the two island sources");
	Check(IslandSourceName(IslandSource::kComponent) == "component", "the component source names itself");
	Check(IslandSourceName(IslandSource::kChart) == "chart", "the chart source names itself");
	Check(PlainIslandSourceName(IslandSource::kComponent) == "part", "the component's plain name is part");
	Check(PlainIslandSourceName(IslandSource::kChart) == "chart", "the chart's plain name is chart");

	const SourceKind componentBake = IslandBakeOf(IslandSource::kComponent);
	const BakeSource* asBake = Get<BakeSource>(componentBake);
	Check(asBake != nullptr, "a component's island bake is a bake source");
	Check(asBake != nullptr && Is<ComponentIdBake>(asBake->bake), "the component source bakes component ids");
	const SourceKind chartBake = IslandBakeOf(IslandSource::kChart);
	const BakeSource* asChart = Get<BakeSource>(chartBake);
	Check(asChart != nullptr && Is<ChartIdBake>(asChart->bake), "the chart source bakes chart ids");

	const MeshData     twoParts = TwoParts();
	const MeshAnalysis twoAnalysis = AnalyseMesh(twoParts);
	Check(twoAnalysis.components == 2, "two disconnected triangle groups are two components");
	Check(twoAnalysis.charts == 2, "the two groups sit in two separate uv charts");
	Check(twoAnalysis.islands.size() == static_cast<std::size_t>(twoAnalysis.components) + twoAnalysis.charts, "the island list holds every component and chart");
	Check(twoAnalysis.componentOf.size() == 6 && twoAnalysis.chartOf.size() == 6, "both label tables cover every vertex");

	const MeshIsland* rootPart = ComponentWithBone(twoAnalysis, "root");
	const MeshIsland* tipPart = ComponentWithBone(twoAnalysis, "tip");
	Check(rootPart != nullptr && tipPart != nullptr, "each component keeps its dominant bone");
	Check(rootPart != nullptr && rootPart->triangles == 1, "a one-triangle group reports one triangle");
	Check(rootPart != nullptr && Near(rootPart->share, 0.5f), "each of two equal groups holds half the triangles");
	Check(rootPart != nullptr && Near(rootPart->dominantShare, 1.0f), "a group welded to a single bone is fully dominated by it");
	Check(rootPart != nullptr && Near(rootPart->centroid.x, 1.0f / 3.0f), "the centroid averages the group's vertex positions");
	Check(rootPart != nullptr && rootPart->twin.has_value(), "a component that fills exactly one chart is twinned to it");

	const MeshData     seam = Seam();
	const MeshAnalysis seamAnalysis = AnalyseMesh(seam);
	Check(seamAnalysis.components == 1, "a shared position seam stays one component");
	Check(seamAnalysis.charts == 2, "a uv seam splits the one component into two charts");
	bool seamComponentUntwinned = true;
	for (const MeshIsland& island : seamAnalysis.islands) {
		if (island.source == IslandSource::kComponent && island.twin.has_value()) {
			seamComponentUntwinned = false;
		}
	}
	Check(seamComponentUntwinned, "a component spanning two charts is twinned to neither");

	const BakeBuffers componentBakeBuffers = BuildIslandBake(twoParts, twoAnalysis, IslandSource::kComponent);
	Check(componentBakeBuffers.problem.empty(), "baking a real analysis reports no problem");
	Check(componentBakeBuffers.vertices.size() == 6, "the bake carries one vertex per mesh vertex");
	Check(componentBakeBuffers.indices.size() == 6, "the bake carries both triangles");
	Check(!componentBakeBuffers.vector, "an island bake writes a scalar id");
	Check(Near(componentBakeBuffers.vertices[0].u, 0.0f) && Near(componentBakeBuffers.vertices[1].u, 0.1f), "the bake carries each vertex's uv");
	const float idA = componentBakeBuffers.vertices[0].value[0];
	const float idB = componentBakeBuffers.vertices[3].value[0];
	Check(!Near(idA, idB), "the two components bake to two different texel ids");
	Check(Near(idA, 0.0f) || Near(idB, 0.0f), "the first-ranked island bakes to id zero");

	MeshAnalysis       empty;
	const BakeBuffers  emptyBake = BuildIslandBake(twoParts, empty, IslandSource::kComponent);
	Check(emptyBake.vertices.empty() && !emptyBake.problem.empty(), "baking an analysis with no regions reports a problem");

	MeshData      smaller;
	MeshPartition oneTriangle;
	oneTriangle.vertices = {
		Vert(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f),
		Vert(1.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0, 0.0f),
		Vert(0.0f, 1.0f, 0.0f, 0.0f, 0.1f, 0, 0.0f),
	};
	oneTriangle.triangles = { { 0, 1, 2 } };
	smaller.partitions = { oneTriangle };
	const MeshAnalysis smallerAnalysis = AnalyseMesh(smaller);
	const BakeBuffers  mismatched = BuildIslandBake(twoParts, smallerAnalysis, IslandSource::kComponent);
	Check(mismatched.problem == "the analysis is of a different mesh", "a table too short for the mesh is refused, not indexed past its end");
	Check(mismatched.vertices.empty() && mismatched.indices.empty(), "the refused bake carries nothing");

	const MeshData     emptyMesh;
	const MeshAnalysis emptyMeshAnalysis = AnalyseMesh(emptyMesh);
	Check(emptyMeshAnalysis == MeshAnalysis{}, "an empty mesh yields an empty analysis");

	MeshData      degenerate;
	MeshPartition bad;
	bad.vertices = { Vert(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f) };
	bad.triangles = { { 7, 8, 9 } };
	degenerate.partitions = { bad };
	const MeshAnalysis degenerateAnalysis = AnalyseMesh(degenerate);
	Check(degenerateAnalysis.components == 0 && degenerateAnalysis.charts == 0, "out-of-range triangles are dropped, leaving no island");

	return test::Finish("islands");
}
