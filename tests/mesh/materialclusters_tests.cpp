#include "mesh/MaterialClusters.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Check(kMaxClusters == 8, "at most eight clusters");
	Check(kMaxSampleTexels == 64 * 64, "the sample caps at 64 by 64 texels");

	MaterialTexel texel;
	texel.roughness = 0.5f;
	Check(texel == texel, "a texel compares equal to itself");

	MaterialSample sample;
	sample.width = 64;
	sample.height = 64;
	sample.texels.push_back(texel);
	Check(sample.texels.size() == 1, "a sample holds its texels");

	ClusterSettings settings;
	Check(settings.clusters == 4 && settings.iterations == 32, "the cluster defaults are exposed");
	Check(test::Near(settings.weights.occlusion, 0.5f), "occlusion is weighted below the full channels");

	MaterialCluster cluster;
	cluster.id = 2;
	cluster.share = 0.25f;
	MaterialAnalysis analysis;
	analysis.settings = settings;
	analysis.clusters.push_back(cluster);
	Check(analysis.clusters.size() == 1, "an analysis holds its clusters");
	Check(MaterialAnalysis{} == MaterialAnalysis{}, "two empty analyses compare equal");

	return test::Finish("materialclusters");
}
