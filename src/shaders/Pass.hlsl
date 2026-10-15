cbuffer PassParams : register(b0)
{
	TextureRead passSource;
	LayerMix    passLayer;
	uint        passMode;
	float       passBelowMip;
	uint        passMapChannel;
	uint        passMapView;
	float       passMapMip;
};

float Curve(float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpEntries(passCurveTexture.Load(int3(lo, 0, 0)).x, passCurveTexture.Load(int3(hi, 0, 0)).x, p);
}

float NormalSlope(float2 uv)
{
	float2 n = passMapTexture.SampleLevel(linearSampler, uv, 0).rg * 2 - 1;
	return saturate(length(n));
}

float4 LayerPass(float2 rawUv)
{
	uint options = passLayer.options;
	float4 below = HasOption(options, kLayerHasBelow) ? passBelowTexture.SampleLevel(linearSampler, rawUv, passBelowMip) : float4(0, 0, 0, 0);
	float4 s = 0;
	if (HasOption(options, kLayerHasSource)) s = passSourceTexture.SampleLevel(linearSampler, ReadUv(passSource, rawUv), passSource.mip);
	float3 value = LayerSourceValue(passSource, passLayer, s);
	if (HasOption(options, kLayerHasCurve)) {
		value = float3(Curve(value.r), Curve(value.g), Curve(value.b));
	}
	float4 m = HasOption(options, kLayerHasMask) ? passMapTexture.SampleLevel(linearSampler, rawUv, 0) : float4(0, 0, 0, 0);
	return MixLayer(below, value, passLayer, m);
}

float4 MapView(float2 uv)
{
	if (passMapView == kMapSlope) { float r = NormalSlope(uv); return float4(r, r, r, 1); }
	if (passMapView == kMapIds) {
		uint width, height;
		passMapTexture.GetDimensions(width, height);
		int2 texel = int2(uv * float2(width, height));
		float id = round(Pick(passMapTexture.Load(int3(texel, 0)), passMapChannel).x * 255);
		return float4(kIdPalette[(uint)id % kGroupCount], 1);
	}
	float4 a = passMapTexture.SampleLevel(linearSampler, uv, passMapMip);
	return float4(Pick(a, passMapChannel), 1);
}

float4 PSMain(VSOut i) : SV_Target
{
	if (passMode == kPassLayer) return LayerPass(i.uv);
	if (passMode == kPassChannel) return MapView(i.uv);
	float4 c = passSourceTexture.SampleLevel(linearSampler, PlaceUv(i.uv, passSource), passSource.mip);
	if (passMode == kPassCopy) return c;
	return float4(c.rgb, 1);
}
