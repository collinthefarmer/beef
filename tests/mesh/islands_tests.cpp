#include "mesh/Islands.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Check(kNoIsland == 0xFFFF, "the no-island sentinel is exposed");
	Check(kMaxIslands == 255, "at most 255 islands fit a byte bake");
	Check(test::Near(kChartWeldUv, 1.0f / 4096.0f), "the chart weld is one texel at 4096");

	Check(std::size(kIslandSources) == 2, "component and chart are the two island sources");
	Check(NameOf(kIslandSources, IslandSource::kComponent) == "component", "the component source names itself");
	Check(kIslandSources[1].plainName == "chart", "the chart source has a plain name");

	MeshIsland island;
	island.id = 3;
	island.source = IslandSource::kChart;
	Check(island == island, "an island compares equal to itself");
	Check(!island.twin.has_value(), "a fresh island has no twin");

	MeshAnalysis analysis;
	Check(analysis.components == 0 && analysis.charts == 0, "an empty analysis counts nothing");
	Check(MeshAnalysis{} == MeshAnalysis{}, "two empty analyses compare equal");

	return test::Finish("islands");
}
