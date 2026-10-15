cbuffer ReduceParams : register(b4)
{
	uint  reduceKind;
	uint  reduceComponents;
	uint2 reduceExtent;
	bool  reduceReadsField;
};

float4 ReductionSample(float4 s, uint components)
{
	float3 v = float3(s.x, components > 1 ? s.y : 0, components > 2 ? s.z : 0);
	bool3 special = (asuint(v) & 0x7f800000) == 0x7f800000;
	return any(special) ? float4(0, 0, 0, 1) : float4(v, 0);
}

float4 PSReduce(VSOut i) : SV_Target
{
	int2 origin = int2(i.pos.xy) * 4;
	int2 extent = int2(reduceExtent);
	float4 result = 0;
	bool seen = false;
	[unroll] for (int dy = 0; dy < 4; ++dy)
	[unroll] for (int dx = 0; dx < 4; ++dx) {
		int2 at = origin + int2(dx, dy);
		if (at.x < extent.x && at.y < extent.y) {
			float4 s = singleSource.Load(int3(at, 0));
			if (reduceReadsField) s = ReductionSample(s, reduceComponents);
			if (!seen) result = s;
			else if (reduceKind == kReduceMinimum) result = float4(min(result.xyz, s.xyz), max(result.w, s.w));
			else if (reduceKind == kReduceMaximum) result = float4(max(result.xyz, s.xyz), max(result.w, s.w));
			else result = float4(result.xyz + s.xyz, max(result.w, s.w));
			seen = true;
		}
	}
	return result;
}
