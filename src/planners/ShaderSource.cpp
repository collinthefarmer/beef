// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ShaderSource.h"

#include "mesh/MaterialClusters.h"
#include "planners/FieldProgram.h"
#include "planners/ProgramShader.h"

#include <format>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
constexpr const char *kShaderSourceHead = R"(
cbuffer Params : register(b0)
{
	float4 offsetScale;
	float4 flags;
	float4 extra;
	float4 extra2;
	float4 layer;
	float4 layerColor;
	float4 layerMask;
	float4 layerCurve;
};
Texture2D    src   : register(t0);
Texture2D    armor : register(t1);
Texture2D    prev  : register(t2);
Texture2D    curve : register(t3);
SamplerState samp  : register(s0);

float LerpEntries(float low, float high, float position)
{
	return low + (high - low) * (position - floor(position));
}

float Curve(float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpEntries(curve.Load(int3(lo, 0, 0)).x, curve.Load(int3(hi, 0, 0)).x, p);
}

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	VSOut o;
	o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	o.uv = uv;
	return o;
}

float NormalSlope(float2 uv)
{
	float2 n = armor.SampleLevel(samp, uv, 0).rg * 2 - 1;
	return saturate(length(n));
}

float3 Pick(float4 s, int channel)
{
	if (channel == 0) return s.rrr;
	if (channel == 1) return s.ggg;
	if (channel == 2) return s.bbb;
	if (channel == 3) return s.aaa;
	if (channel == 5) return dot(s.rgb, float3(0.2126, 0.7152, 0.0722)).xxx;
	return s.rgb;
}

float3 Blend(int mode, float3 below, float3 value)
{
	if (mode == 1) return below * value;
	if (mode == 2) return below + value;
	if (mode == 3) return below - value;
	if (mode == 4) return 1 - (1 - below) * (1 - value);
	if (mode == 6) {
		float3 t = below * 2 - float3(1, 1, 0);
		float3 u = value * float3(-2, -2, 2) + float3(1, 1, -1);
		float3 r = t * (dot(t, u) / max(t.z, 0.001)) - u;
		float len = length(r);
		return (len > 0 ? r / len : float3(0, 0, 1)) * 0.5 + 0.5;
	}
	return value;
}

float3 LayerValue(float4 color, bool hasSource, float4 s, int channel)
{
	float3 value = color.rgb;
	if (hasSource) value *= Pick(s, channel) * color.w;
	return value;
}

float4 ComposeLayer(float4 below, float3 value, float opacity, bool hasMask, float4 m, int maskChannel, int blend, int bits)
{
	float alpha = opacity;
	if (hasMask) alpha *= Pick(m, maskChannel).x;
	float3 mixed = lerp(below.rgb, Blend(blend, below.rgb, value), saturate(alpha));
	float4 result = below;
	if (bits & 1) result.r = mixed.r;
	if (bits & 2) result.g = mixed.g;
	if (bits & 4) result.b = mixed.b;
	if (bits & 8) result.a = lerp(below.a, dot(value, float3(0.2126, 0.7152, 0.0722)), saturate(alpha));
	return result;
}

float4 LayerPass(float2 rawUv, float2 placedUv)
{
	float4 below = layerMask.z > 0.5 ? prev.SampleLevel(samp, rawUv, extra2.x) : float4(0, 0, 0, 0);
	float4 s = 0;
	if (layerMask.w > 0.5) {
		float2 uv = layer.y > 0.5 ? rawUv : placedUv;
		s = src.SampleLevel(samp, uv, extra.x);
	}
	float3 value = LayerValue(layerColor, layerMask.w > 0.5, s, (int)layer.x);
	if (layerCurve.x > 0.5) {
		value = float3(Curve(value.r), Curve(value.g), Curve(value.b));
	}
	float4 m = layerMask.x >= 0 ? armor.SampleLevel(samp, rawUv, 0) : float4(0, 0, 0, 0);
	return ComposeLayer(below, value, layer.w, layerMask.x >= 0, m, (int)layerMask.x, (int)layer.z, (int)layerMask.y);
}

float4 PSMain(VSOut i) : SV_Target
{
	float2 uv = i.uv;
	if (flags.z > 0.5) uv = uv.yx;
	if (flags.x > 0.5) uv.x = 1 - uv.x;
	if (flags.y > 0.5) uv.y = 1 - uv.y;
	uv = uv * offsetScale.zw + offsetScale.xy;
	float4 c = src.SampleLevel(samp, uv, extra.x);
	int mode = (int)flags.w;
	if (mode == 6) return LayerPass(i.uv, uv);
	if (mode == 7) return c;
	if (mode == 4) {
		if (extra.w > 0.5) { float r = NormalSlope(i.uv); return float4(r, r, r, 1); }
		if (extra2.z > 0.5) {
			uint width, height;
			armor.GetDimensions(width, height);
			int2 texel = int2(i.uv * float2(width, height));
			float id = round(Pick(armor.Load(int3(texel, 0)), (int)extra.z).x * 255);
			return float4(kIdPalette[(uint)id % 8], 1);
		}
		float4 a = armor.SampleLevel(samp, i.uv, extra2.y);
		return float4(Pick(a, (int)extra.z), 1);
	}
	return float4(c.rgb, 1);
}
cbuffer ProgramParams : register(b1)
{
	float4 code[256];
	float4 inputs[16];
	float4 inputValues[16];
	float4 texParams[8];
	float4 texTransform[8];
	float4 texFlags[8];
	float4 misc;
};
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
Texture2D tex5 : register(t5);
Texture2D tex6 : register(t6);
Texture2D tex7 : register(t7);
Texture2D lookup0 : register(t8);
Texture2D lookup1 : register(t9);
Texture2D lookup2 : register(t10);
Texture2D lookup3 : register(t11);

float4 SampleSlot(int slot, float2 uv, float mip)
{
	switch (slot) {
	case 0: return tex0.SampleLevel(samp, uv, mip);
	case 1: return tex1.SampleLevel(samp, uv, mip);
	case 2: return tex2.SampleLevel(samp, uv, mip);
	case 3: return tex3.SampleLevel(samp, uv, mip);
	case 4: return tex4.SampleLevel(samp, uv, mip);
	case 5: return tex5.SampleLevel(samp, uv, mip);
	case 6: return tex6.SampleLevel(samp, uv, mip);
	default: return tex7.SampleLevel(samp, uv, mip);
	}
}

float LookupLoad(int slot, int at)
{
	int3 texel = int3(at, 0, 0);
	switch (slot) {
	case 0: return lookup0.Load(texel).x;
	case 1: return lookup1.Load(texel).x;
	case 2: return lookup2.Load(texel).x;
	default: return lookup3.Load(texel).x;
	}
}

float LookupAt(int slot, float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpEntries(LookupLoad(slot, lo), LookupLoad(slot, hi), p);
}

float2 PlaceUv(float2 uv, float4 tr, float4 fl)
{
	if (fl.z > 0.5) uv = uv.yx;
	if (fl.x > 0.5) uv.x = 1 - uv.x;
	if (fl.y > 0.5) uv.y = 1 - uv.y;
	return uv * tr.zw + tr.xy;
}

float2 SlotSize(int slot)
{
	float w = 0, h = 0;
	switch (slot) {
	case 0: tex0.GetDimensions(w, h); break;
	case 1: tex1.GetDimensions(w, h); break;
	case 2: tex2.GetDimensions(w, h); break;
	case 3: tex3.GetDimensions(w, h); break;
	case 4: tex4.GetDimensions(w, h); break;
	case 5: tex5.GetDimensions(w, h); break;
	case 6: tex6.GetDimensions(w, h); break;
	default: tex7.GetDimensions(w, h); break;
	}
	return float2(w, h);
}

float2 TexelCentre(int slot, float2 uv)
{
	float2 size = SlotSize(slot);
	if (size.x < 1 || size.y < 1) return uv;
	return (floor(uv * size) + 0.5) / size;
}

float3 ReadTexture(int slot, float2 rawUv)
{
	float4 p = texParams[slot];
	float2 uv = p.y > 0.5 ? rawUv : PlaceUv(rawUv, texTransform[slot], texFlags[slot]);
	if (texFlags[slot].w > 0.5) uv = TexelCentre(slot, uv);
	return Pick(SampleSlot(slot, uv, p.w), (int)p.x) * p.z;
}

float3 SafeDiv(float3 a, float3 b)
{
	return float3(b.x == 0 ? 0 : a.x / b.x, b.y == 0 ? 0 : a.y / b.y, b.z == 0 ? 0 : a.z / b.z);
}

float PowOne(float a, float b)
{
	if (b == 0 || a == 1) return 1;
	float r = exp2(log2(abs(a)) * b);
	if (a < 0) {
		if (b != floor(b)) return 0;
		if (frac(b * 0.5) != 0) r = -r;
	}
	return isfinite(r) ? r : 0;
}

float3 SafePow(float3 a, float3 b)
{
	return float3(PowOne(a.x, b.x), PowOne(a.y, b.y), PowOne(a.z, b.z));
}

uint PrimeAtLeast(uint n)
{
	uint p = max(n, 2u);
	[loop] for (uint gap = 0; gap < 16; ++gap) {
		bool prime = true;
		[loop] for (uint d = 2; d * d <= p; ++d) {
			if (p % d == 0) { prime = false; break; }
		}
		if (prime) break;
		++p;
	}
	return p;
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

uint HashBits(float value)
{
	return (isfinite(value) && value != 0) ? asuint(value) : 0u;
}

float Hash(float value, float salt)
{
	uint mixed = MixBits(HashBits(value) ^ MixBits(HashBits(salt) + 0x9e3779b9u));
	return (float)(mixed >> 8) * (1.0 / 16777216.0);
}

float RandRange(float start, float stop, float seed)
{
	if (!isfinite(start)) return 0;
	float lo = floor(start);
	float hi = isfinite(stop) ? floor(stop) : lo;
	if (!(hi > lo)) return lo;
	float s = isfinite(seed) ? clamp(seed, 0, 1) : 0;
	float span = hi - lo;
	return lo + min(floor(s * span), span - 1);
}

float WheelChannel(float turns, float offset)
{
	return saturate(abs(frac(turns + offset) * 6 - 3) - 1);
}

float3 Hue(float turns)
{
	float t = isfinite(turns) ? turns : 0;
	return float3(WheelChannel(t, 0), WheelChannel(t, 2.0 / 3.0), WheelChannel(t, 1.0 / 3.0));
}

float3 HueShift(float3 color, float turns)
{
	float angle = (isfinite(turns) ? turns : 0) * 6.28318530718;
	float c = cos(angle);
	float s = sin(angle);
	float k = 0.57735026919;
	float along = k * (color.x + color.y + color.z) * (1 - c);
	float3 across = k * float3(color.z - color.y, color.x - color.z, color.y - color.x);
	float3 shifted = color * c + across * s + k * along;
	return isfinite(shifted) ? saturate(shifted) : 0;
}

float Shuffle(float index, float total, float seed)
{
	if (!(total >= 1) || !(index >= 0) || isinf(index)) return 256;
	float n = min(floor(total), 251);
	float i = floor(index);
	if (i >= n) return 256;
	uint count = (uint)n;
	uint p = PrimeAtLeast(count);
	float s = isfinite(seed) ? frac(seed) : 0;
	uint scale = 1 + min((uint)floor(frac(s * 29.41) * (p - 1)), p - 2);
	uint shift = min((uint)floor(frac(s * 13.37) * p), p - 1);
	uint x = (uint)i;
	[loop] for (uint k = 0; k < p; ++k) {
		x = (scale * x + shift) % p;
		if (x < count) break;
	}
	return (float)x;
}

float3 RunProgram(int first, int count, float2 uv)
{
	float3 st[)";

constexpr const char *kShaderSourceMid = R"(];
	int    sp = 0;
	int    end = min(first + count, 256);
	[loop] for (int k = max(first, 0); k < end; ++k) {
		float4 c = code[k];
		int    op = (int)c.x;
		int    idx = (int)c.z;
		int    packed = (int)c.w;
		int    components = packed & 3;
		int    pops = packed >> 2;
		float3 a = 0, b = 0, d = 0;
		if (pops >= 1) { if (sp > 0) { --sp; a = st[sp]; } }
		if (pops >= 2) { if (sp > 0) { --sp; b = st[sp]; } }
		if (pops >= 3) { if (sp > 0) { --sp; d = st[sp]; } }
		float3 r = 0;
)";

constexpr const char *kShaderSourceAfterZRule = R"(
		if (sp < )";

constexpr const char *kShaderSourceTail = R"() { st[sp] = r; ++sp; }
	}
	return sp > 0 ? st[sp - 1] : float3(0, 0, 0);
}

float4 PSProgram(VSOut i) : SV_Target
{
	float3 result = RunProgram(0, (int)misc.y, i.uv);
	return misc.z > 0.5 ? float4(result, 1) : float4(result.xxx, 1);
}

struct BakeIn  { float2 uv : TEXCOORD0; float3 value : COLOR0; };
struct BakeOut { float4 pos : SV_Position; float3 value : COLOR0; };

BakeOut BakeVS(BakeIn i)
{
	BakeOut o;
	o.pos = float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, 0, 1);
	o.value = i.value;
	return o;
}

float4 BakePS(BakeOut i) : SV_Target
{
	return float4(i.value, 1);
}

cbuffer BakeNoiseParams : register(b7)
{
	uint4 bakeNoise;
};

uint LatticeHash(int3 c, uint seed)
{
	uint h = asuint(c.x) * 73856093u ^ asuint(c.y) * 19349663u ^ asuint(c.z) * 83492791u ^ seed * 2654435761u;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

float LatticeValue(int3 c, uint seed)
{
	return (float)(LatticeHash(c, seed) >> 8) * (1.0 / 16777216.0);
}

float ValueNoise(float3 p, uint seed)
{
	float3 cell = floor(p);
	int3 c = int3(cell);
	float3 f = p - cell;
	float3 u = f * f * (3.0 - 2.0 * f);
	float y0z0 = lerp(LatticeValue(c, seed), LatticeValue(c + int3(1, 0, 0), seed), u.x);
	float y1z0 = lerp(LatticeValue(c + int3(0, 1, 0), seed), LatticeValue(c + int3(1, 1, 0), seed), u.x);
	float y0z1 = lerp(LatticeValue(c + int3(0, 0, 1), seed), LatticeValue(c + int3(1, 0, 1), seed), u.x);
	float y1z1 = lerp(LatticeValue(c + int3(0, 1, 1), seed), LatticeValue(c + int3(1, 1, 1), seed), u.x);
	return lerp(lerp(y0z0, y1z0, u.y), lerp(y0z1, y1z1, u.y), u.z);
}

float FractalNoise(float3 p, uint octaves, uint seed)
{
	float sum = 0;
	float total = 0;
	float amplitude = 1;
	float frequency = 1;
	[unroll] for (uint o = 0; o < 4; ++o) {
		if (o < octaves) {
			sum += amplitude * ValueNoise(p * frequency, seed + o * 7919u);
			total += amplitude;
		}
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	return sum / total;
}

float CellularEdge(float3 p, uint seed)
{
	float3 cell = floor(p);
	int3 c = int3(cell);
	float nearest = 8.0;
	float second = 8.0;
	[unroll] for (int dz = -1; dz <= 1; ++dz)
	[unroll] for (int dy = -1; dy <= 1; ++dy)
	[unroll] for (int dx = -1; dx <= 1; ++dx) {
		int3 n = c + int3(dx, dy, dz);
		float3 feature = float3(n) + float3(
			LatticeValue(n, seed + 1013u),
			LatticeValue(n, seed + 2026u),
			LatticeValue(n, seed + 3039u));
		float d = length(feature - p);
		if (d < nearest) {
			second = nearest;
			nearest = d;
		} else if (d < second) {
			second = d;
		}
	}
	return saturate(second - nearest);
}

float CellularNoise(float3 p, uint octaves, uint seed)
{
	float sum = 0;
	float total = 0;
	float amplitude = 1;
	float frequency = 1;
	[loop] for (uint o = 0; o < 4; ++o) {
		if (o < octaves) {
			sum += amplitude * CellularEdge(p * frequency, seed + o * 7919u);
			total += amplitude;
		}
		amplitude *= 0.5;
		frequency *= 2.0;
	}
	return sum / total;
}

float StyledNoise(float3 p, uint octaves, uint seed, uint style)
{
	if (style == 2u)
		return CellularNoise(p, octaves, seed);
	float n = FractalNoise(p, octaves, seed);
	return style == 1u ? abs(2.0 * n - 1.0) : n;
}

float4 BakeNoisePS(BakeOut i) : SV_Target
{
	uint octaves = clamp(bakeNoise.x, 1u, 4u);
	uint seed = bakeNoise.y;
	uint style = min(bakeNoise.z, 2u);
	return float4(
		StyledNoise(i.value, octaves, seed, style),
		StyledNoise(i.value + float3(17.3, 5.9, 31.1), octaves, seed + 101u, style),
		StyledNoise(i.value + float3(41.7, 23.3, 11.9), octaves, seed + 202u, style),
		1);
}

float4 DilatePS(VSOut i) : SV_Target
{
	int3 at = int3(int2(i.pos.xy), 0);
	float4 c = src.Load(at);
	if (c.a > 0.5) return c;
	float3 sum = 0;
	float n = 0;
	[unroll] for (int dy = -1; dy <= 1; ++dy)
	[unroll] for (int dx = -1; dx <= 1; ++dx) {
		float4 s = src.Load(at + int3(dx, dy, 0));
		if (s.a > 0.5) { sum += s.rgb; n += 1; }
	}
	return n > 0 ? float4(sum / n, 1) : float4(0, 0, 0, 0);
}

cbuffer ReduceParams : register(b4)
{
	uint4 reduceShape;
	uint4 reduceFlags;
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
	int2 extent = int2(reduceShape.zw);
	float4 result = 0;
	bool seen = false;
	[unroll] for (int dy = 0; dy < 4; ++dy)
	[unroll] for (int dx = 0; dx < 4; ++dx) {
		int2 at = origin + int2(dx, dy);
		if (at.x < extent.x && at.y < extent.y) {
			float4 s = src.Load(int3(at, 0));
			if (reduceFlags.x != 0) s = ReductionSample(s, reduceShape.y);
			if (!seen) result = s;
			else if (reduceShape.x == 2) result = float4(min(result.xyz, s.xyz), max(result.w, s.w));
			else if (reduceShape.x == 3) result = float4(max(result.xyz, s.xyz), max(result.w, s.w));
			else result = float4(result.xyz + s.xyz, max(result.w, s.w));
			seen = true;
		}
	}
	return result;
}

cbuffer RippleParams : register(b2)
{
	float4 rippleFirings[8];
	float4 rippleShape;
	float4 rippleMisc;
	float4 rippleDir;
};

float4 PSRipple(VSOut i) : SV_Target
{
	float3 pos = (src.SampleLevel(samp, i.uv, 0).rgb * 2 - 1) * rippleMisc.y;
	float  v = 0;
	int    n = (int)rippleMisc.x;
	float  width = max(rippleShape.y, 0.01);
	bool   directional = rippleDir.w > 0.5;
	[loop] for (int k = 0; k < n && k < 8; ++k) {
		float3 offset = pos - rippleFirings[k].xyz;
		float  d = directional ? dot(offset, rippleDir.xyz) : length(offset);
		float  r = rippleFirings[k].w * rippleShape.x;
		float  f;
		if (rippleShape.w > 0.5) {
			float lead = 1 - smoothstep(r - width, r, d);
			float trail = directional ? smoothstep(-width, 0, d) : 1;
			f = lead * trail;
		} else {
			float x = (d - r) / width;
			f = exp(-x * x * 4);
		}
		v = max(v, f * exp(-rippleShape.z * rippleFirings[k].w));
	}
	return float4(v, v, v, 1);
}

cbuffer StackParams : register(b5)
{
	float4 stackOffsetScale[8];
	float4 stackFlags[8];
	float4 stackLayer[8];
	float4 stackColor[8];
	float4 stackMask[8];
	float4 stackField[8];
	float4 stackMisc;
};
Texture2D stackBase : register(t12);
Texture2D stackSourceTexture0 : register(t13);
Texture2D stackSourceTexture1 : register(t14);
Texture2D stackSourceTexture2 : register(t15);
Texture2D stackSourceTexture3 : register(t16);
Texture2D stackSourceTexture4 : register(t17);
Texture2D stackSourceTexture5 : register(t18);
Texture2D stackSourceTexture6 : register(t19);
Texture2D stackSourceTexture7 : register(t20);
Texture2D stackMaskTexture0 : register(t21);
Texture2D stackMaskTexture1 : register(t22);
Texture2D stackMaskTexture2 : register(t23);
Texture2D stackMaskTexture3 : register(t24);
Texture2D stackMaskTexture4 : register(t25);
Texture2D stackMaskTexture5 : register(t26);
Texture2D stackMaskTexture6 : register(t27);
Texture2D stackMaskTexture7 : register(t28);

float4 SampleStackSource(int k, float2 uv, float mip)
{
	switch (k) {
	case 0: return stackSourceTexture0.SampleLevel(samp, uv, mip);
	case 1: return stackSourceTexture1.SampleLevel(samp, uv, mip);
	case 2: return stackSourceTexture2.SampleLevel(samp, uv, mip);
	case 3: return stackSourceTexture3.SampleLevel(samp, uv, mip);
	case 4: return stackSourceTexture4.SampleLevel(samp, uv, mip);
	case 5: return stackSourceTexture5.SampleLevel(samp, uv, mip);
	case 6: return stackSourceTexture6.SampleLevel(samp, uv, mip);
	default: return stackSourceTexture7.SampleLevel(samp, uv, mip);
	}
}

float4 SampleStackMask(int k, float2 uv)
{
	switch (k) {
	case 0: return stackMaskTexture0.SampleLevel(samp, uv, 0);
	case 1: return stackMaskTexture1.SampleLevel(samp, uv, 0);
	case 2: return stackMaskTexture2.SampleLevel(samp, uv, 0);
	case 3: return stackMaskTexture3.SampleLevel(samp, uv, 0);
	case 4: return stackMaskTexture4.SampleLevel(samp, uv, 0);
	case 5: return stackMaskTexture5.SampleLevel(samp, uv, 0);
	case 6: return stackMaskTexture6.SampleLevel(samp, uv, 0);
	default: return stackMaskTexture7.SampleLevel(samp, uv, 0);
	}
}

float4 Unorm8(float4 v)
{
	return round(saturate(v) * 255) / 255;
}

float4 PSStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = stackMisc.y > 0.5 ? stackBase.SampleLevel(samp, rawUv, stackMisc.z) : float4(0, 0, 0, 0);
	int n = (int)stackMisc.x;
	[loop] for (int k = 0; k < n && k < 8; ++k) {
		bool hasSource = stackMask[k].w > 0.5;
		bool hasMask = stackMask[k].x >= 0;
		float4 s = 0;
		float4 m = 0;
		if (stackField[k].y > 0.5) s = float4(RunProgram((int)stackField[k].x, (int)stackField[k].y, rawUv), 1);
		if (stackField[k].w > 0.5) m = float4(RunProgram((int)stackField[k].z, (int)stackField[k].w, rawUv), 1);
		if (hasSource && stackField[k].y < 0.5) {
			float2 uv = stackLayer[k].y > 0.5 ? rawUv : PlaceUv(rawUv, stackOffsetScale[k], stackFlags[k]);
			s = SampleStackSource(k, uv, stackFlags[k].w);
		}
		float3 value = LayerValue(stackColor[k], hasSource, s, (int)stackLayer[k].x);
		if (hasMask && stackField[k].w < 0.5) m = SampleStackMask(k, rawUv);
		float4 result = ComposeLayer(below, value, stackLayer[k].w, hasMask, m, (int)stackMask[k].x, (int)stackLayer[k].z, (int)stackMask[k].y);
		below = k + 1 < n ? Unorm8(result) : result;
	}
	return below;
}

cbuffer GroupParams : register(b3)
{
	float4 groupCentre[2];
	float4 groupSpread[2];
	float4 groupMean[16];
	float4 groupWhitening[72];
	float4 groupInfo[8];
	float4 groupMisc;
	float4 groupPalette[8];
	float4 groupLevels;
	float4 groupDetail;
	float4 groupCharts[64];
};

float ChartLitAt(float2 uv)
{
	uint w, h;
	tex3.GetDimensions(w, h);
	int2 texel = min(int2(uv * float2(w, h)), int2(w, h) - 1);
	int chart = clamp((int)round(tex3.Load(int3(texel, 0)).r * 255.0), 0, 255);
	return groupCharts[chart >> 2][chart & 3];
}

float NormalDetailAt(float2 uv, float4 detail)
{
	if (detail.x < 0)
		return 0;
	float2 coarse = prev.SampleLevel(samp, uv, detail.y).xy * 2 - 1;
	float total = 0;
	[unroll] for (int y = 0; y < 4; ++y) {
		[unroll] for (int x = 0; x < 4; ++x) {
			float2 offset = (float2(x, y) - 1.5) * (detail.z * 0.25);
			float2 fine = prev.SampleLevel(samp, uv + offset, detail.x).xy * 2 - 1;
			total += length(fine - coarse);
		}
	}
	return saturate(total * (2.0 / 16.0));
}

cbuffer DiffuseDetailParams : register(b6)
{
	float4 diffuseDetailLevels;
	float4 diffuseDetailNormal;
};

float4 PSDiffuseDetail(VSOut i) : SV_Target
{
	float3 d = src.SampleLevel(samp, i.uv, diffuseDetailLevels.x).rgb;
	return float4(d, NormalDetailAt(i.uv, diffuseDetailNormal));
}

float4 PSClusters(VSOut i) : SV_Target
{
	float4 m = saturate(armor.SampleLevel(samp, i.uv, groupLevels.x));
	float3 d = saturate(src.SampleLevel(samp, i.uv, groupLevels.y).rgb);
	float axes[8];
	axes[0] = m.r;
	axes[1] = m.g;
	axes[2] = m.b;
	axes[3] = m.a;
	axes[4] = saturate(dot(d, float3(0.2126, 0.7152, 0.0722)));
	axes[5] = saturate(0.5 + d.r - d.b);
	axes[6] = saturate(0.5 + d.g - 0.5 * (d.r + d.b));
	axes[7] = NormalDetailAt(i.uv, groupDetail);
	float z[8];
	[unroll] for (int a = 0; a < 8; ++a)
		z[a] = (axes[a] - groupCentre[a >> 2][a & 3]) / groupSpread[a >> 2][a & 3];
	float4 odds0 = tex4.SampleLevel(samp, i.uv, 0);
	float4 odds1 = tex5.SampleLevel(samp, i.uv, 0);
	int n = clamp((int)groupMisc.x, 0, 8);
	float id = 0;
	float best = 0;
	[loop] for (int k = 0; k < n; ++k) {
		float sum = 0;
		[unroll] for (int r = 0; r < 8; ++r) {
			float row = 0;
			[unroll] for (int c = 0; c <= r; ++c) {
				int w = k * 36 + r * (r + 1) / 2 + c;
				row += groupWhitening[w >> 2][w & 3] * (z[c] - groupMean[k * 2 + (c >> 2)][c & 3]);
			}
			sum += row * row;
		}
		int g = clamp((int)groupInfo[k].x, 0, 7);
		float odds = g < 4 ? odds0[g] : odds1[g - 4];
		float score = groupInfo[k].y - 0.5 * sum + groupMisc.w * log(max(odds, 1.0 / 512.0));
		if (k == 0 || score > best) {
			best = score;
			id = groupInfo[k].x;
		}
	}
	if (groupMisc.z > 0.5)
		return float4(groupPalette[(uint)id % 8].rgb, 1);
	float v = groupMisc.y > 0.5
	              ? (abs(id - (groupMisc.y - 1.0)) < 0.5 ? 1.0 : 0.0)
	              : id / 255.0;
	if (groupLevels.z > 0.5)
		v *= ChartLitAt(i.uv);
	return float4(v, v, v, 1);
}
)";

std::string IdPaletteSource() {
  std::string source = "static const float3 kIdPalette[8] = {";
  for (std::size_t k = 0; k < kClusterPalette.size(); ++k) {
    const Vec3 &colour = kClusterPalette[k];
    source += std::format("{}float3({}, {}, {})", k == 0 ? "" : ", ", colour.x,
                          colour.y, colour.z);
  }
  return source + "};\n";
}

const std::string kShaderSourceStorage =
    IdPaletteSource() + std::string(kShaderSourceHead) +
    std::to_string(kProgramStack) + kShaderSourceMid + InterpreterSwitch() +
    InterpreterZRule() + kShaderSourceAfterZRule +
    std::to_string(kProgramStack) + kShaderSourceTail;
}

extern const char *const kShaderSource = kShaderSourceStorage.c_str();
}
