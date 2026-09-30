// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "planners/ProgramShader.h"

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

float LerpCurve(float low, float high, float position)
{
	return low + (high - low) * (position - floor(position));
}

float Curve(float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpCurve(curve.Load(int3(lo, 0, 0)).x, curve.Load(int3(hi, 0, 0)).x, p);
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

float Relief(int input, float2 uv)
{
	float4 a = armor.SampleLevel(samp, uv, 0);
	if (input == 4) {
		float2 n = a.rg * 2 - 1;
		return sqrt(saturate(1 - dot(n, n)));
	}
	return 0.5;
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
	float4 below = layerMask.z > 0.5 ? prev.SampleLevel(samp, rawUv, 0) : float4(0, 0, 0, 0);
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
		if (extra.w > 0.5) { float r = Relief(4, i.uv); return float4(r, r, r, 1); }
		float4 a = armor.SampleLevel(samp, i.uv, 0);
		return float4(Pick(a, (int)extra.z), 1);
	}
	return float4(c.rgb, 1);
}
cbuffer ProgramParams : register(b1)
{
	float4 code[256];
	float4 refs[16];
	float4 refValues[16];
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
Texture2D lut0 : register(t8);
Texture2D lut1 : register(t9);
Texture2D lut2 : register(t10);
Texture2D lut3 : register(t11);

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

float LutLoad(int slot, int at)
{
	int3 texel = int3(at, 0, 0);
	switch (slot) {
	case 0: return lut0.Load(texel).x;
	case 1: return lut1.Load(texel).x;
	case 2: return lut2.Load(texel).x;
	default: return lut3.Load(texel).x;
	}
}

float LutAt(int slot, float v)
{
	float p = saturate(v) * 255;
	int lo = (int)p;
	int hi = min(lo + 1, 255);
	return LerpCurve(LutLoad(slot, lo), LutLoad(slot, hi), p);
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

constexpr const char *kShaderSourceAfterSwitch = R"(
		if (components == 2 && op != 38 && op != 39 && op != 40) r.z = 0;
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
Texture2D stackSrc0 : register(t13);
Texture2D stackSrc1 : register(t14);
Texture2D stackSrc2 : register(t15);
Texture2D stackSrc3 : register(t16);
Texture2D stackSrc4 : register(t17);
Texture2D stackSrc5 : register(t18);
Texture2D stackSrc6 : register(t19);
Texture2D stackSrc7 : register(t20);
Texture2D stackMap0 : register(t21);
Texture2D stackMap1 : register(t22);
Texture2D stackMap2 : register(t23);
Texture2D stackMap3 : register(t24);
Texture2D stackMap4 : register(t25);
Texture2D stackMap5 : register(t26);
Texture2D stackMap6 : register(t27);
Texture2D stackMap7 : register(t28);

float4 StackSource(int k, float2 uv, float mip)
{
	switch (k) {
	case 0: return stackSrc0.SampleLevel(samp, uv, mip);
	case 1: return stackSrc1.SampleLevel(samp, uv, mip);
	case 2: return stackSrc2.SampleLevel(samp, uv, mip);
	case 3: return stackSrc3.SampleLevel(samp, uv, mip);
	case 4: return stackSrc4.SampleLevel(samp, uv, mip);
	case 5: return stackSrc5.SampleLevel(samp, uv, mip);
	case 6: return stackSrc6.SampleLevel(samp, uv, mip);
	default: return stackSrc7.SampleLevel(samp, uv, mip);
	}
}

float4 StackMap(int k, float2 uv)
{
	switch (k) {
	case 0: return stackMap0.SampleLevel(samp, uv, 0);
	case 1: return stackMap1.SampleLevel(samp, uv, 0);
	case 2: return stackMap2.SampleLevel(samp, uv, 0);
	case 3: return stackMap3.SampleLevel(samp, uv, 0);
	case 4: return stackMap4.SampleLevel(samp, uv, 0);
	case 5: return stackMap5.SampleLevel(samp, uv, 0);
	case 6: return stackMap6.SampleLevel(samp, uv, 0);
	default: return stackMap7.SampleLevel(samp, uv, 0);
	}
}

float4 Unorm8(float4 v)
{
	return round(saturate(v) * 255) / 255;
}

float4 PSStack(VSOut i) : SV_Target
{
	float2 rawUv = i.uv;
	float4 below = stackMisc.y > 0.5 ? stackBase.SampleLevel(samp, rawUv, 0) : float4(0, 0, 0, 0);
	int n = (int)stackMisc.x;
	[loop] for (int k = 0; k < n && k < 8; ++k) {
		bool hasSource = stackMask[k].w > 0.5;
		bool hasMask = stackMask[k].x >= 0;
		float4 s = 0;
		float4 m = 0;
		[loop] for (int f = 0; f < 2; ++f) {
			int count = (int)(f == 0 ? stackField[k].y : stackField[k].w);
			if (count > 0) {
				float4 v = float4(RunProgram((int)(f == 0 ? stackField[k].x : stackField[k].z), count, rawUv), 1);
				if (f == 0) s = v; else m = v;
			}
		}
		if (hasSource && stackField[k].y < 0.5) {
			float2 uv = stackLayer[k].y > 0.5 ? rawUv : PlaceUv(rawUv, stackOffsetScale[k], stackFlags[k]);
			s = StackSource(k, uv, stackFlags[k].w);
		}
		float3 value = LayerValue(stackColor[k], hasSource, s, (int)stackLayer[k].x);
		if (hasMask && stackField[k].w < 0.5) m = StackMap(k, rawUv);
		float4 result = ComposeLayer(below, value, stackLayer[k].w, hasMask, m, (int)stackMask[k].x, (int)stackLayer[k].z, (int)stackMask[k].y);
		below = k + 1 < n ? Unorm8(result) : result;
	}
	return below;
}

cbuffer ClusterParams : register(b3)
{
	float4 centroidRmaos[8];
	float4 centroidLuma[8];
	float4 centroidDiffuse[8];
	float4 clusterWeights;
	float4 clusterMisc;
};

float4 PSClusters(VSOut i) : SV_Target
{
	float4 m = saturate(armor.SampleLevel(samp, i.uv, 0));
	float3 d = saturate(src.SampleLevel(samp, i.uv, 0).rgb);
	float  luma = saturate(dot(d, float3(0.2126, 0.7152, 0.0722)));
	int    n = clamp((int)clusterMisc.y, 0, 8);
	float  id = 0;
	float  best = 0;
	[loop] for (int k = 0; k < n; ++k) {
		float4 dm = m - centroidRmaos[k];
		float  dl = luma - centroidLuma[k].x;
		float3 dc = d - centroidDiffuse[k].rgb;
		float  dist = dot(clusterWeights, dm * dm) + clusterMisc.x * dl * dl
		              + clusterMisc.z * dot(dc, dc);
		if (k == 0 || dist < best) {
			best = dist;
			id = centroidLuma[k].y;
		}
	}
	float v = id / 255.0;
	return float4(v, v, v, 1);
}
)";

const std::string kShaderSourceStorage =
    std::string(kShaderSourceHead) + std::to_string(TextureLab::kProgramStack) +
    kShaderSourceMid + InterpreterSwitch() + kShaderSourceAfterSwitch +
    std::to_string(TextureLab::kProgramStack) + kShaderSourceTail;
}

extern const char *const kShaderSource = kShaderSourceStorage.c_str();
}
