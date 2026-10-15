struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	VSOut o;
	o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	o.uv = uv;
	return o;
}

struct TextureRead
{
	float2 offset;
	float2 tile;
	float  mip;
	float  normalize;
	uint   channel;
	uint   options;
};

struct LayerMix
{
	float3 color;
	float  opacity;
	uint   blend;
	uint   writeChannels;
	uint   maskChannel;
	uint   options;
};

struct NormalDetail
{
	float fineMip;
	float coarseMip;
	float footprint;
	bool  present;
};

bool HasOption(uint options, uint option)
{
	return (options & option) != 0;
}

float2 PlaceUv(float2 uv, TextureRead read)
{
	if (HasOption(read.options, kReadTranspose)) uv = uv.yx;
	if (HasOption(read.options, kReadMirrorU)) uv.x = 1 - uv.x;
	if (HasOption(read.options, kReadMirrorV)) uv.y = 1 - uv.y;
	return uv * read.tile + read.offset;
}

float2 ReadUv(TextureRead read, float2 rawUv)
{
	return HasOption(read.options, kReadMeshSpace) ? rawUv : PlaceUv(rawUv, read);
}

float LerpEntries(float low, float high, float position)
{
	return low + (high - low) * (position - floor(position));
}

float Luminance(float3 rgb)
{
	return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float3 Pick(float4 s, uint channel)
{
	if (channel == kChannelR) return s.rrr;
	if (channel == kChannelG) return s.ggg;
	if (channel == kChannelB) return s.bbb;
	if (channel == kChannelA) return s.aaa;
	if (channel == kChannelLuma) return Luminance(s.rgb).xxx;
	return s.rgb;
}

float3 Reorient(float3 below, float3 value)
{
	float3 t = below * 2 - float3(1, 1, 0);
	float3 u = value * float3(-2, -2, 2) + float3(1, 1, -1);
	float3 r = t * (dot(t, u) / max(t.z, 0.001)) - u;
	float len = length(r);
	return (len > 0 ? r / len : float3(0, 0, 1)) * 0.5 + 0.5;
}

float3 Blend(uint mode, float3 below, float3 value)
{
	if (mode == kBlendMultiply) return below * value;
	if (mode == kBlendAdd) return below + value;
	if (mode == kBlendSubtract) return below - value;
	if (mode == kBlendScreen) return 1 - (1 - below) * (1 - value);
	if (mode == kBlendReorient) return Reorient(below, value);
	return value;
}

float3 LayerValue(float3 color, float normalize, bool hasSource, float4 s, uint channel)
{
	float3 value = color;
	if (hasSource) value *= Pick(s, channel) * normalize;
	return value;
}

float4 ComposeLayer(float4 below, float3 value, float opacity, bool hasMask, float4 m, uint maskChannel, uint blend, uint writeChannels)
{
	float alpha = opacity;
	if (hasMask) alpha *= Pick(m, maskChannel).x;
	float3 mixed = lerp(below.rgb, Blend(blend, below.rgb, value), saturate(alpha));
	float4 result = below;
	if (HasOption(writeChannels, kWriteR)) result.r = mixed.r;
	if (HasOption(writeChannels, kWriteG)) result.g = mixed.g;
	if (HasOption(writeChannels, kWriteB)) result.b = mixed.b;
	if (HasOption(writeChannels, kWriteA)) result.a = lerp(below.a, Luminance(value), saturate(alpha));
	return result;
}

float3 LayerSourceValue(TextureRead source, LayerMix mix, float4 s)
{
	return LayerValue(mix.color, source.normalize, HasOption(mix.options, kLayerHasSource), s, source.channel);
}

float4 MixLayer(float4 below, float3 value, LayerMix mix, float4 m)
{
	return ComposeLayer(below, value, mix.opacity, HasOption(mix.options, kLayerHasMask), m, mix.maskChannel, mix.blend, mix.writeChannels);
}

float4 Unorm8(float4 v)
{
	return round(saturate(v) * 255) / 255;
}

uint MixBits(uint x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}
