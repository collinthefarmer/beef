struct GroupScore
{
	float4 mean[2];
	float4 whitening[kGroupWhiteningRows];
	uint   id;
	float  logScale;
	uint2  padding;
};

cbuffer GroupParams : register(b3)
{
	GroupScore   groups[kGroupCount];
	float4       groupAxisCentre[2];
	float4       groupAxisSpread[2];
	uint4        groupLitCharts[kLitChartRows];
	NormalDetail groupDetail;
	uint         groupCount;
	uint         groupDraw;
	uint         groupPicked;
	float        groupOddsWeight;
	float        groupRmaosMip;
	float        groupDiffuseMip;
	bool         groupChartFilter;
};

float Lane(float4 rows[2], int at)
{
	return rows[at >> 2][at & 3];
}

bool IsChartLit(uint chart)
{
	uint word = chart >> 5;
	uint bits = groupLitCharts[word >> 2][word & 3];
	return ((bits >> (chart & 31)) & 1) != 0;
}

float ChartLitAt(float2 uv)
{
	uint w, h;
	groupChartIds.GetDimensions(w, h);
	int2 texel = min(int2(uv * float2(w, h)), int2(w, h) - 1);
	uint chart = (uint)clamp((int)round(groupChartIds.Load(int3(texel, 0)).r * 255.0), 0, 255);
	return IsChartLit(chart) ? 1.0 : 0.0;
}

float4 PSClusters(VSOut i) : SV_Target
{
	float4 m = saturate(materialRmaos.SampleLevel(linearSampler, i.uv, groupRmaosMip));
	float3 d = saturate(materialDiffuse.SampleLevel(linearSampler, i.uv, groupDiffuseMip).rgb);
	float axes[kTexelAxisCount];
	axes[0] = m.r;
	axes[1] = m.g;
	axes[2] = m.b;
	axes[3] = m.a;
	axes[4] = saturate(Luminance(d));
	axes[5] = saturate(0.5 + d.r - d.b);
	axes[6] = saturate(0.5 + d.g - 0.5 * (d.r + d.b));
	axes[7] = NormalDetailAt(i.uv, groupDetail);
	float z[kTexelAxisCount];
	[unroll] for (int a = 0; a < (int)kTexelAxisCount; ++a)
		z[a] = (axes[a] - Lane(groupAxisCentre, a)) / Lane(groupAxisSpread, a);
	float4 odds0 = groupOddsLow.SampleLevel(linearSampler, i.uv, 0);
	float4 odds1 = groupOddsHigh.SampleLevel(linearSampler, i.uv, 0);
	int n = (int)min(groupCount, kGroupCount);
	uint id = 0;
	float best = 0;
	[loop] for (int k = 0; k < n; ++k) {
		GroupScore group = groups[k];
		float sum = 0;
		[unroll] for (int r = 0; r < (int)kTexelAxisCount; ++r) {
			float row = 0;
			[unroll] for (int c = 0; c <= r; ++c) {
				int w = r * (r + 1) / 2 + c;
				row += group.whitening[w >> 2][w & 3] * (z[c] - Lane(group.mean, c));
			}
			sum += row * row;
		}
		uint g = min(group.id, kGroupCount - 1);
		float odds = g < 4 ? odds0[g] : odds1[g - 4];
		float score = group.logScale - 0.5 * sum + groupOddsWeight * log(max(odds, 1.0 / 512.0));
		if (k == 0 || score > best) {
			best = score;
			id = group.id;
		}
	}
	if (groupDraw == kGroupDrawPalette)
		return float4(kIdPalette[id % kGroupCount], 1);
	float v = groupDraw == kGroupDrawPicked
	              ? (id == groupPicked ? 1.0 : 0.0)
	              : id / 255.0;
	if (groupChartFilter)
		v *= ChartLitAt(i.uv);
	return float4(v, v, v, 1);
}
