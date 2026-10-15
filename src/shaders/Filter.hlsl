cbuffer FilterParams : register(b8)
{
	uint  filterOperation;
	uint  filterTaps;
	bool  filterVertical;
	bool  filterKeepSeams;
	uint2 filterTargetSize;
	uint2 filterInputSize;
	bool  filterResamples;
	float4 filterWeights[kFilterWeightRows];
};

float FilterWeight(int k)
{
	uint at = (uint)abs(k);
	return filterWeights[at >> 2][at & 3];
}

int2 FilterSource(int2 at)
{
	if (!filterResamples) return at;
	int2 target = int2(max(filterTargetSize, 1u));
	int2 input = int2(max(filterInputSize, 1u));
	return clamp(at * input / target, 0, input - 1);
}

float4 FilterRead(int2 at, bool vertical)
{
	float4 s = filterInput.Load(int3(FilterSource(at), 0));
	return vertical ? s : s.xxxx;
}

float4 FilterFinish(float4 sweep, uint operation)
{
	if (operation == kFilterGrow) return float4(sweep.zzz, 1);
	if (operation == kFilterShrink) return float4(sweep.yyy, 1);
	if (operation == kFilterEdge) { float e = sweep.z - sweep.y; return float4(e, e, e, 1); }
	return float4(sweep.xxx, 1);
}

float4 PSFilter(VSOut i) : SV_Target
{
	int taps = (int)min(filterTaps, kMaxFilterTaps);
	bool vertical = filterVertical;
	bool keepSeams = filterKeepSeams;
	int2 target = int2(max(filterTargetSize, 1u));
	int2 at = int2(i.pos.xy);
	int2 step = vertical ? int2(0, 1) : int2(1, 0);
	float chart = keepSeams ? filterCharts.Load(int3(at, 0)).r : 0;
	float4 own = FilterRead(at, vertical);
	float sum = 0, weight = 0;
	float lowest = own.y, highest = own.z;
	[loop] for (int k = -taps; k <= taps; ++k) {
		int2 near = clamp(at + step * k, 0, target - 1);
		if (keepSeams && filterCharts.Load(int3(near, 0)).r != chart) continue;
		float4 v = FilterRead(near, vertical);
		float wk = FilterWeight(k);
		sum += v.x * wk;
		weight += wk;
		lowest = min(lowest, v.y);
		highest = max(highest, v.z);
	}
	float4 sweep = float4(weight > 0 ? sum / weight : own.x, lowest, highest, 1);
	return vertical ? FilterFinish(sweep, filterOperation) : sweep;
}
