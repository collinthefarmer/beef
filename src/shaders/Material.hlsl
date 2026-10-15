float NormalDetailAt(float2 uv, NormalDetail detail)
{
	if (!detail.present)
		return 0;
	float2 coarse = materialNormal.SampleLevel(linearSampler, uv, detail.coarseMip).xy * 2 - 1;
	float total = 0;
	[unroll] for (int y = 0; y < 4; ++y) {
		[unroll] for (int x = 0; x < 4; ++x) {
			float2 offset = (float2(x, y) - 1.5) * (detail.footprint * 0.25);
			float2 fine = materialNormal.SampleLevel(linearSampler, uv + offset, detail.fineMip).xy * 2 - 1;
			total += length(fine - coarse);
		}
	}
	return saturate(total * (2.0 / 16.0));
}

cbuffer DiffuseDetailParams : register(b6)
{
	NormalDetail diffuseDetailNormal;
	float        diffuseDetailMip;
};

float4 PSDiffuseDetail(VSOut i) : SV_Target
{
	float3 d = materialDiffuse.SampleLevel(linearSampler, i.uv, diffuseDetailMip).rgb;
	return float4(d, NormalDetailAt(i.uv, diffuseDetailNormal));
}
