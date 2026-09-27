// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

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

float Curve(float v)
{
	return curve.SampleLevel(samp, float2(saturate(v) * (255.0 / 256.0) + 0.5 / 256.0, 0.5), 0).x;
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
		return normalize(r) * 0.5 + 0.5;
	}
	return value;
}

float4 LayerPass(float2 rawUv, float2 placedUv)
{
	float4 below = layerMask.z > 0.5 ? prev.SampleLevel(samp, rawUv, 0) : float4(0, 0, 0, 0);
	float3 value = layerColor.rgb;
	if (layerMask.w > 0.5) {
		float2 uv = layer.y > 0.5 ? rawUv : placedUv;
		float4 s = src.SampleLevel(samp, uv, extra.x);
		value *= Pick(s, (int)layer.x) * layerColor.w;
	}
	if (layerCurve.x > 0.5) {
		value = float3(Curve(value.r), Curve(value.g), Curve(value.b));
	}
	float alpha = layer.w;
	if (layerMask.x >= 0) {
		float4 m = armor.SampleLevel(samp, rawUv, 0);
		alpha *= Pick(m, (int)layerMask.x).x;
	}
	float3 mixed = lerp(below.rgb, Blend((int)layer.z, below.rgb, value), saturate(alpha));
	int bits = (int)layerMask.y;
	float4 result = below;
	if (bits & 1) result.r = mixed.r;
	if (bits & 2) result.g = mixed.g;
	if (bits & 4) result.b = mixed.b;
	if (bits & 8) result.a = lerp(below.a, dot(value, float3(0.2126, 0.7152, 0.0722)), saturate(alpha));
	return result;
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

float LutAt(int slot, float v)
{
	float2 uv = float2(saturate(v) * (255.0 / 256.0) + 0.5 / 256.0, 0.5);
	switch (slot) {
	case 0: return lut0.SampleLevel(samp, uv, 0).x;
	case 1: return lut1.SampleLevel(samp, uv, 0).x;
	case 2: return lut2.SampleLevel(samp, uv, 0).x;
	default: return lut3.SampleLevel(samp, uv, 0).x;
	}
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

float3 SafePow(float3 a, float3 b)
{
	float3 r = pow(a, b);
	return float3(isfinite(r.x) ? r.x : 0, isfinite(r.y) ? r.y : 0, isfinite(r.z) ? r.z : 0);
}

float4 PSProgram(VSOut i) : SV_Target
{
	float3 st[)";

constexpr const char *kShaderSourceMid = R"(];
	int    sp = 0;
	int    n = (int)misc.y;
	[loop] for (int k = 0; k < n; ++k) {
		float4 c = code[k];
		int    op = (int)c.x;
		int    idx = (int)c.z;
		int    components = (int)c.w;
		float3 a = 0, b = 0, d = 0;
		int pops = 0;
		switch (op) {
		case 0: case 3: pops = 0; break;
		case 4: case 8: case 9: case 23: case 27: case 28: case 29: case 30: case 31: case 33: case 34: case 38: case 42: pops = 1; break;
		case 1: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21:
		case 24: case 25: case 32: case 35: case 39: case 40: case 41: pops = 2; break;
		default: pops = 3; break;
		}
		if (pops >= 1) { if (sp > 0) { --sp; a = st[sp]; } }
		if (pops >= 2) { if (sp > 0) { --sp; b = st[sp]; } }
		if (pops >= 3) { if (sp > 0) { --sp; d = st[sp]; } }
		float3 r = 0;
		switch (op) {
		case 0:  r = c.y; break;
		case 1:  r = float3(b.x, a.x, 0); break;
		case 2:  r = float3(d.x, b.x, a.x); break;
		case 3:  r = refs[idx].x > 0.5 ? ReadTexture((int)refs[idx].y, i.uv) : refValues[idx].xyz; break;
		case 4:  r = LutAt(idx, a.x); break;
		case 8:  r = -a; break;
		case 9:  r = a.x > 0 ? 0 : 1; break;
		case 10: r = b + a; break;
		case 11: r = b - a; break;
		case 12: r = b * a; break;
		case 13: r = SafeDiv(b, a); break;
		case 14: r = b.x < a.x ? 1 : 0; break;
		case 15: r = b.x > a.x ? 1 : 0; break;
		case 16: r = b.x <= a.x ? 1 : 0; break;
		case 17: r = b.x >= a.x ? 1 : 0; break;
		case 18: r = b.x == a.x ? 1 : 0; break;
		case 19: r = b.x != a.x ? 1 : 0; break;
		case 20: r = (b.x > 0 ? 1 : 0) * (a.x > 0 ? 1 : 0); break;
		case 21: r = max(b.x > 0 ? 1 : 0, a.x > 0 ? 1 : 0); break;
		case 22: r = d.x > 0 ? b : a; break;
		case 23: r = abs(a); break;
		case 24: r = min(b, a); break;
		case 25: r = max(b, a); break;
		case 26: r = clamp(d, b, a); break;
		case 27: r = saturate(a); break;
		case 28: r = floor(a); break;
		case 29: r = ceil(a); break;
		case 30: r = frac(a); break;
		case 31: r = sqrt(max(0, a)); break;
		case 32: r = SafePow(b, a); break;
		case 33: r = sin(a); break;
		case 34: r = cos(a); break;
		case 35: r = float3(a.x < b.x ? 0 : 1, a.y < b.y ? 0 : 1, a.z < b.z ? 0 : 1); break;
		case 36: r = smoothstep(d, b, a); break;
		case 37: r = lerp(d, b, a); break;
		case 38: r = components == 2 ? length(a.xy) : length(a); break;
		case 39: r = components == 2 ? length(b.xy - a.xy) : length(b - a); break;
		case 40: r = components == 2 ? dot(b.xy, a.xy) : dot(b, a); break;
		case 41: r = cross(b, a); break;
		default: r = SafeDiv(a, components == 2 ? length(a.xy) : length(a)); break;
		}
		if (components == 2 && op != 38 && op != 39 && op != 40) r.z = 0;
		if (sp < )";

constexpr const char *kShaderSourceTail = R"() { st[sp] = r; ++sp; }
	}
	float3 result = sp > 0 ? st[sp - 1] : 0;
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
			f = exp(-pow((d - r) / width, 2) * 4);
		}
		v = max(v, f * exp(-rippleShape.z * rippleFirings[k].w));
	}
	return float4(v, v, v, 1);
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
    kShaderSourceMid + std::to_string(TextureLab::kProgramStack) +
    kShaderSourceTail;
}

extern const char *const kShaderSource = kShaderSourceStorage.c_str();
}
