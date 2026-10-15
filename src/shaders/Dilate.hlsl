float4 DilatePS(VSOut i) : SV_Target
{
	int3 at = int3(int2(i.pos.xy), 0);
	float4 c = singleSource.Load(at);
	if (c.a > 0.5) return c;
	float3 sum = 0;
	float n = 0;
	[unroll] for (int dy = -1; dy <= 1; ++dy)
	[unroll] for (int dx = -1; dx <= 1; ++dx) {
		float4 s = singleSource.Load(at + int3(dx, dy, 0));
		if (s.a > 0.5) { sum += s.rgb; n += 1; }
	}
	return n > 0 ? float4(sum / n, 1) : float4(0, 0, 0, 0);
}
