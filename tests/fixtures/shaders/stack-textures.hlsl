float4 PSGeneratedStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = float4(0, 0, 0, 0);
	{
		float4 s = 0;
		float4 m = 0;
		float2 uv = PlaceUv(rawUv, stackLayers[0].source);
		s = SampleStackSource(0, uv, stackLayers[0].source.mip);
		float3 value = LayerValue(stackLayers[0].mix.color, stackLayers[0].source.normalize, true, s, 4);
		m = SampleStackMask(0, rawUv);
		float4 result = ComposeLayer(below, value, stackLayers[0].mix.opacity, true, m, 0, 1, 15);
		below = Unorm8(result);
	}
	{
		float4 s = 0;
		float4 m = 0;
		float3 value = LayerValue(stackLayers[1].mix.color, stackLayers[1].source.normalize, false, s, 0);
		float4 result = ComposeLayer(below, value, stackLayers[1].mix.opacity, false, m, 0, 4, 7);
		below = Unorm8(result);
	}
	{
		float4 s = 0;
		float4 m = 0;
		float2 uv = rawUv;
		s = SampleStackSource(2, uv, stackLayers[2].source.mip);
		float3 value = LayerValue(stackLayers[2].mix.color, stackLayers[2].source.normalize, true, s, 0);
		float4 result = ComposeLayer(below, value, stackLayers[2].mix.opacity, false, m, 0, 0, 15);
		below = result;
	}
	return below;
}
