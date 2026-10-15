struct StackLayer
{
	TextureRead source;
	LayerMix    mix;
	uint2       sourceCode;
	uint2       maskCode;
};

cbuffer StackParams : register(b5)
{
	StackLayer stackLayers[kStackLayerCount];
	uint       stackLayerCount;
	bool       stackHasBase;
	float      stackBaseMip;
};

float4 SampleStackBase(float2 rawUv)
{
	return stackBaseTexture.SampleLevel(linearSampler, rawUv, stackBaseMip);
}

float4 PSStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = stackHasBase ? SampleStackBase(rawUv) : float4(0, 0, 0, 0);
	int n = (int)min(stackLayerCount, kStackLayerCount);
	[loop] for (int k = 0; k < n; ++k) {
		StackLayer layer = stackLayers[k];
		float4 s = 0;
		float4 m = 0;
		if (layer.sourceCode.y > 0) s = float4(RunProgram((int)layer.sourceCode.x, (int)layer.sourceCode.y, rawUv), 1);
		else if (HasOption(layer.mix.options, kLayerHasSource)) s = SampleStackSource(k, ReadUv(layer.source, rawUv), layer.source.mip);
		if (layer.maskCode.y > 0) m = float4(RunProgram((int)layer.maskCode.x, (int)layer.maskCode.y, rawUv), 1);
		else if (HasOption(layer.mix.options, kLayerHasMask)) m = SampleStackMask(k, rawUv);
		float4 result = MixLayer(below, LayerSourceValue(layer.source, layer.mix, s), layer.mix, m);
		below = k + 1 < n ? Unorm8(result) : result;
	}
	return below;
}
