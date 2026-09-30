float4 PSGeneratedStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = float4(0, 0, 0, 0);
	{
		float4 s = 0;
		float4 m = 0;
		float2 uv = PlaceUv(rawUv, stackOffsetScale[0], stackFlags[0]);
		s = StackSource(0, uv, stackFlags[0].w);
		float3 value = LayerValue(stackColor[0], true, s, 4);
		m = StackMap(0, rawUv);
		float4 result = ComposeLayer(below, value, stackLayer[0].w, true, m, 0, 1, 15);
		below = Unorm8(result);
	}
	{
		float4 s = 0;
		float4 m = 0;
		float3 value = LayerValue(stackColor[1], false, s, 0);
		float4 result = ComposeLayer(below, value, stackLayer[1].w, false, m, 0, 4, 7);
		below = Unorm8(result);
	}
	{
		float4 s = 0;
		float4 m = 0;
		float2 uv = rawUv;
		s = StackSource(2, uv, stackFlags[2].w);
		float3 value = LayerValue(stackColor[2], true, s, 0);
		float4 result = ComposeLayer(below, value, stackLayer[2].w, false, m, 0, 0, 15);
		below = result;
	}
	return below;
}
