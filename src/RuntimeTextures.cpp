#include "RuntimeTextures.h"

#include "Identity.h"

#include <REX/W32/D3DCOMPILER.h>

#include <cmath>
#include <cstring>
#include <string_view>

namespace WornEnchantmentPBR
{
	using namespace REX::W32;
	// These four are also forward-declared at global scope by RE/N/NiTexture.h.
	using REX::W32::ID3D11DepthStencilView;
	using REX::W32::ID3D11RenderTargetView;
	using REX::W32::ID3D11ShaderResourceView;
	using REX::W32::ID3D11Texture2D;

	namespace
	{
		constexpr bool Failed(std::int32_t a_hr) noexcept { return a_hr < 0; }
	}

	namespace
	{
		constexpr std::uint32_t kShellCount = 512;  // slot_000..slot_511.dds

		// Full-screen triangle; the pixel shader tiles, mirrors, transposes and
		// scrolls the source exactly as tools/make_flipbook.py bakes a frame.
		constexpr const char* kShaderSource = R"(
cbuffer Params : register(b0)
{
	float4 offsetScale; // xy uv offset, zw tile scale
	float4 flags;       // x mirrorU, y mirrorV, z transpose, w mode
	float4 extra;       // x source mip, y armor input (0 none, 1 displacement.r, 2 ao.b, 3 rmaos),
	                    // zw per mode: height = armor weight, noise weight; roughness = strength, contrast
	float4 extra2;      // height: x relief mean, y relief contrast, z noise mean
	                    // masked glow: extra.z channel, extra.w threshold; extra2 = softness, invert, strength
	                    // channel: extra.z channel (as Pick reads it), extra.w slope instead
	float4 layer;       // layer pass: x source channel (as Pick reads it), y mesh space, z blend mode, w opacity
	float4 layerColor;  // layer pass: rgb colour, w normalise factor
	float4 layerMask;   // layer pass: x mask channel (-1 none), y channel bits, z has previous, w has source
	float4 layerCurve;  // layer pass: x has curve
};
Texture2D    src   : register(t0);
Texture2D    armor : register(t1);
Texture2D    prev  : register(t2);
Texture2D    curve : register(t3);  // 256 x 1, the layer's curve over 0..1
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

// Relief of the armor input at the raw UV, 0..1 (higher = raised).
float Relief(int input, float2 uv)
{
	float4 a = armor.SampleLevel(samp, uv, 0);
	if (input == 1) return a.r;                         // displacement map
	if (input == 2) return a.b;                         // RMAOS occlusion
	if (input == 4) {                                   // normal slope
		float2 n = a.rg * 2 - 1;
		return sqrt(saturate(1 - dot(n, n)));
	}
	if (input == 5) return dot(a.rgb, float3(0.299, 0.587, 0.114));  // diffuse luminance
	return 0.5;
}

// One armor channel shaped into a 0..1 mask at the raw UV.
float Mask(float2 uv)
{
	float4 a = armor.SampleLevel(samp, uv, 0);
	int    ch = (int)extra.z;
	float  v = ch == 0 ? a.r : ch == 1 ? a.g : ch == 2 ? a.b : a.a;
	float  t = extra.w;
	if (t > 0) v = smoothstep(t - max(extra2.x, 1e-4), t + max(extra2.x, 1e-4), v);
	if (extra2.y > 0.5) v = 1 - v;
	return lerp(1, v, extra2.z);
}

// One channel of a sample as a broadcast value, or its rgb.
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
		// Reoriented normal mapping: the value's normal, rotated so its up
		// follows the normal below; both maps are 0..1 tangent-space encodings.
		float3 t = below * 2 - float3(1, 1, 0);
		float3 u = value * float3(-2, -2, 2) + float3(1, 1, -1);
		float3 r = t * (dot(t, u) / max(t.z, 0.001)) - u;
		return normalize(r) * 0.5 + 0.5;
	}
	return value;  // replace and lerp: lerp is the opacity mix below
}

float4 LayerPass(float2 rawUv, float2 placedUv)
{
	// A stack without a base starts transparent black: alpha is the fuzz
	// weight, the coat strength or the subsurface thickness, and nothing has
	// been laid down yet.
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
	float l = dot(c.rgb, float3(0.299, 0.587, 0.114));
	int mode = (int)flags.w;
	if (mode == 6) return LayerPass(i.uv, uv);
	if (mode == 7) return c;  // every channel of the source at the given mip
	if (mode == 1) return float4(1, 1, 1, l);
	if (mode == 2) {
		// Height = the armor's own relief plus the scrolling noise, so parallax
		// follows the piece's real depth and the enchantment rides on it.
		int   armorInput = (int)extra.y;
		float relief = armorInput == 0 ? extra2.x : Relief(armorInput, i.uv);
		// Centred so neither input pins the field at the clamp; the relief is
		// stretched around its own mean because occlusion maps sit near white.
		float h = 0.5 + (relief - extra2.x) * extra.z * extra2.y + (l - extra2.z) * extra.w;
		h = saturate(h);
		return float4(h, h, h, 1);
	}
	if (mode == 4) {
		// One channel of the armor input (extra.z, as Pick reads it), or its
		// slope as a normal map (extra.w).
		if (extra.w > 0.5) { float r = Relief(4, i.uv); return float4(r, r, r, 1); }
		float4 a = armor.SampleLevel(samp, i.uv, 0);
		return float4(Pick(a, (int)extra.z), 1);
	}
	if (mode == 5) return float4(c.rgb * Mask(i.uv), 1);
	if (mode == 3) {
		// Armor RMAOS with roughness (r) pulled toward smooth where the noise is
		// bright; metallic, occlusion and reflectance pass through untouched.
		float4 a = armor.SampleLevel(samp, i.uv, 0);
		float  n = saturate((l - 0.5) * extra.w + 0.5);
		a.r = saturate(a.r * (1 - extra.z * n));
		return a;
	}
	return float4(c.rgb, 1);
}
// ------------------------------------------------------------ interpreter
// The same postfix program the CPU evaluator runs (Expression.h), per
// texel. Every stack value is a float3: a scalar is broadcast, so
// component-wise arithmetic matches the CPU's broadcast rule. Comparisons
// and logic are scalar by the type checker and read .x.
cbuffer ProgramParams : register(b1)
{
	float4 code[256];        // x op, y number, z index
	float4 refs[16];         // x 1 = texture read (y slot), 0 = value
	float4 refValues[16];    // the value, broadcast
	float4 texParams[8];     // x channel, y mesh space, z normalise, w mip
	float4 texTransform[8];  // xy uv offset, zw tile
	float4 texFlags[8];      // x mirror u, y mirror v, z transpose
	float4 misc;             // x time, y op count, z vector result
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

float3 ReadTexture(int slot, float2 rawUv)
{
	float4 p = texParams[slot];
	float2 uv = p.y > 0.5 ? rawUv : PlaceUv(rawUv, texTransform[slot], texFlags[slot]);
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
	float3 st[32];
	int    sp = 0;
	int    n = (int)misc.y;
	[loop] for (int k = 0; k < n; ++k) {
		float4 c = code[k];
		int    op = (int)c.x;
		int    idx = (int)c.z;
		float3 a = 0, b = 0, d = 0;
		// Operand counts per op; a pop from an empty stack reads 0.
		int pops = 0;
		switch (op) {
		case 0: case 3: case 5: case 6: case 7: pops = 0; break;
		case 4: case 8: case 9: case 23: case 27: case 28: case 29: case 30: case 31: case 33: case 34: pops = 1; break;
		case 1: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21:
		case 24: case 25: case 32: case 35: pops = 2; break;
		default: pops = 3; break;  // [x,y,z], if, clamp, smoothstep, lerp
		}
		if (pops >= 1) { if (sp > 0) { --sp; a = st[sp]; } }
		if (pops >= 2) { if (sp > 0) { --sp; b = st[sp]; } }
		if (pops >= 3) { if (sp > 0) { --sp; d = st[sp]; } }
		// After the pops: for a binary op, b is the first operand and a the
		// second; for a ternary op, d, b, a in order.
		float3 r = 0;
		switch (op) {
		case 0:  r = c.y; break;                                   // number
		case 1:  r = float3(b.x, a.x, 0); break;                   // [x, y]
		case 2:  r = float3(d.x, b.x, a.x); break;                 // [x, y, z]
		case 3:  r = refs[idx].x > 0.5 ? ReadTexture((int)refs[idx].y, i.uv) : refValues[idx].xyz; break;
		case 4:  r = LutAt(idx, a.x); break;                       // @curve(x)
		case 5:  r = 0; break;                                     // x: not per texel
		case 6:  r = 0.5; break;                                   // mean
		case 7:  r = misc.x; break;                                // time
		case 8:  r = -a; break;
		case 9:  r = a.x > 0 ? 0 : 1; break;                       // not
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
		case 20: r = (b.x > 0 ? 1 : 0) * (a.x > 0 ? 1 : 0); break;  // and
		case 21: r = max(b.x > 0 ? 1 : 0, a.x > 0 ? 1 : 0); break;  // or
		case 22: r = d.x > 0 ? b : a; break;                        // if(c, a, b)
		case 23: r = abs(a); break;
		case 24: r = min(b, a); break;
		case 25: r = max(b, a); break;
		case 26: r = clamp(d, b, a); break;                         // clamp(x, lo, hi)
		case 27: r = saturate(a); break;
		case 28: r = floor(a); break;
		case 29: r = ceil(a); break;
		case 30: r = frac(a); break;
		case 31: r = sqrt(max(0, a)); break;
		case 32: r = SafePow(b, a); break;
		case 33: r = sin(a); break;
		case 34: r = cos(a); break;
		case 35: r = float3(a.x < b.x ? 0 : 1, a.y < b.y ? 0 : 1, a.z < b.z ? 0 : 1); break;  // step(edge, x)
		case 36: r = smoothstep(d, b, a); break;                    // smoothstep(lo, hi, x)
		default: r = lerp(d, b, a); break;                          // lerp(a, b, t)
		}
		if (sp < 32) { st[sp] = r; ++sp; }
	}
	float3 result = sp > 0 ? st[sp - 1] : 0;
	return misc.z > 0.5 ? float4(result, 1) : float4(result.xxx, 1);
}

// ------------------------------------------------------------------- bakes
// Mesh triangles drawn with their UV as the position, carrying a value.
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

// ----------------------------------------------------------------- ripples
// Each live firing is a front at distance age x speed from its origin; a
// ring is a band of the given width, a disc everything inside the front.
// Fronts fade by exp(-decay x age) and combine by max. src is the
// position bake: rgb = position / (2 frame) + 0.5.
cbuffer RippleParams : register(b2)
{
	float4 rippleFirings[8];  // xyz origin (units), w age (s)
	float4 rippleShape;       // x speed, y width, z decay, w 1 = disc
	float4 rippleMisc;        // x firing count, y frame
};

float4 PSRipple(VSOut i) : SV_Target
{
	float3 pos = (src.SampleLevel(samp, i.uv, 0).rgb * 2 - 1) * rippleMisc.y;
	float  v = 0;
	int    n = (int)rippleMisc.x;
	float  width = max(rippleShape.y, 0.01);
	[loop] for (int k = 0; k < n && k < 8; ++k) {
		float d = length(pos - rippleFirings[k].xyz);
		float r = rippleFirings[k].w * rippleShape.x;
		float f = rippleShape.w > 0.5 ? 1 - smoothstep(r - width, r, d) : exp(-pow((d - r) / width, 2) * 4);
		v = max(v, f * exp(-rippleShape.z * rippleFirings[k].w));
	}
	return float4(v, v, v, 1);
}

// ---------------------------------------------------------------- classify
// The RMAOS (armor) and diffuse (src) maps at the raw mesh UV go to the
// nearest centroid by the distance NearestCluster (Analysis.cpp) uses:
// the sum over the five axes of weight x (texel - centroid)^2, the
// first of equals winning, in the analysis' cluster order. That CPU
// function is the reference: this must agree with it on a texel. The
// cluster's id is written as id / 255 grey.
cbuffer ClassifyParams : register(b3)
{
	float4 centroidRmaos[8];  // per cluster in analysis order: roughness, metallic, occlusion, reflectance
	float4 centroidLuma[8];   // x luma, y id
	float4 classifyWeights;   // roughness, metallic, occlusion, reflectance
	float4 classifyMisc;      // x luma weight, y cluster count
};

float4 PSClassify(VSOut i) : SV_Target
{
	float4 m = saturate(armor.SampleLevel(samp, i.uv, 0));
	float3 d = src.SampleLevel(samp, i.uv, 0).rgb;
	float  luma = saturate(dot(d, float3(0.2126, 0.7152, 0.0722)));
	int    n = clamp((int)classifyMisc.y, 0, 8);
	float  id = 0;
	float  best = 0;
	[loop] for (int k = 0; k < n; ++k) {
		float4 dm = m - centroidRmaos[k];
		float  dl = luma - centroidLuma[k].x;
		float  dist = dot(classifyWeights, dm * dm) + classifyMisc.x * dl * dl;
		if (k == 0 || dist < best) {
			best = dist;
			id = centroidLuma[k].y;
		}
	}
	float v = id / 255.0;
	return float4(v, v, v, 1);
}
)";

		struct alignas(16) ProgramConstants
		{
			float code[256][4];
			float refs[16][4];
			float refValues[16][4];
			float texParams[8][4];
			float texTransform[8][4];
			float texFlags[8][4];
			float misc[4];
		};
		static_assert(sizeof(ProgramConstants) % 16 == 0);
		// The shader's op numbers are the enum's; the switch above is written to it.
		static_assert(static_cast<int>(Program::Op::kNumber) == 0 && static_cast<int>(Program::Op::kRef) == 3 && static_cast<int>(Program::Op::kIf) == 22 &&
		              static_cast<int>(Program::Op::kClamp) == 26 && static_cast<int>(Program::Op::kStep) == 35 && static_cast<int>(Program::Op::kLerp) == 37);

		struct alignas(16) RippleConstants
		{
			float firings[8][4];
			float shape[4];
			float misc[4];
		};

		// The shader's arrays are written to kMaxClusters entries.
		struct alignas(16) ClassifyConstants
		{
			float centroidRmaos[kMaxClusters][4];
			float centroidLuma[kMaxClusters][4];
			float weights[4];
			float misc[4];
		};
		static_assert(kMaxClusters == 8 && sizeof(ClassifyConstants) % 16 == 0);

		struct alignas(16) Constants
		{
			float offsetScale[4];
			float flags[4];
			float extra[4];
			float extra2[4];
			float layer[4];
			float layerColor[4];
			float layerMask[4];
			float layerCurve[4];
		};

		template <class T>
		void Release(T*& a_ptr)
		{
			if (a_ptr) {
				a_ptr->Release();
				a_ptr = nullptr;
			}
		}

		RE::NiTexture::RendererData* DataOf(RE::NiSourceTexture* a_texture)
		{
			return a_texture ? reinterpret_cast<RE::NiTexture::RendererData*>(a_texture->rendererTexture) : nullptr;
		}
	}

	// Everything the pass touches on the immediate context, put back afterwards
	// because the engine's state cache does not know we were here.
	struct TextureLab::SavedState
	{
		ID3D11RenderTargetView*   rtv = nullptr;
		ID3D11DepthStencilView*   dsv = nullptr;
		D3D11_VIEWPORT            viewport{};
		std::uint32_t             viewportCount = 1;
		ID3D11VertexShader*       vs = nullptr;
		ID3D11PixelShader*        ps = nullptr;
		ID3D11ShaderResourceView* srvs[12]{};
		ID3D11SamplerState*       sampler = nullptr;
		ID3D11Buffer*             cbs[4]{};
		ID3D11InputLayout*        layout = nullptr;
		REX::W32::ID3D11Buffer*   vertexBuffer = nullptr;
		std::uint32_t             vertexStride = 0;
		std::uint32_t             vertexOffset = 0;
		REX::W32::ID3D11Buffer*   indexBuffer = nullptr;
		DXGI_FORMAT               indexFormat{};
		std::uint32_t             indexOffset = 0;
		D3D11_PRIMITIVE_TOPOLOGY  topology{};
		ID3D11BlendState*         blend = nullptr;
		float                     blendFactor[4]{};
		std::uint32_t             sampleMask = 0;
		ID3D11DepthStencilState*  depth = nullptr;
		std::uint32_t             stencilRef = 0;
		ID3D11RasterizerState*    raster = nullptr;

		void Capture(ID3D11DeviceContext* a_ctx)
		{
			a_ctx->OMGetRenderTargets(1, &rtv, &dsv);
			viewportCount = 1;
			a_ctx->RSGetViewports(&viewportCount, &viewport);
			a_ctx->VSGetShader(&vs, nullptr, nullptr);
			a_ctx->PSGetShader(&ps, nullptr, nullptr);
			a_ctx->PSGetShaderResources(0, 12, srvs);
			a_ctx->PSGetSamplers(0, 1, &sampler);
			a_ctx->PSGetConstantBuffers(0, 4, cbs);
			a_ctx->IAGetInputLayout(&layout);
			a_ctx->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
			a_ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
			a_ctx->IAGetPrimitiveTopology(&topology);
			a_ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
			a_ctx->OMGetDepthStencilState(&depth, &stencilRef);
			a_ctx->RSGetState(&raster);
		}

		void Restore(ID3D11DeviceContext* a_ctx)
		{
			a_ctx->OMSetRenderTargets(1, &rtv, dsv);
			if (viewportCount) {
				a_ctx->RSSetViewports(1, &viewport);
			}
			a_ctx->VSSetShader(vs, nullptr, 0);
			a_ctx->PSSetShader(ps, nullptr, 0);
			a_ctx->PSSetShaderResources(0, 12, srvs);
			a_ctx->PSSetSamplers(0, 1, &sampler);
			a_ctx->PSSetConstantBuffers(0, 4, cbs);
			a_ctx->IASetInputLayout(layout);
			a_ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
			a_ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
			a_ctx->IASetPrimitiveTopology(topology);
			a_ctx->OMSetBlendState(blend, blendFactor, sampleMask);
			a_ctx->OMSetDepthStencilState(depth, stencilRef);
			a_ctx->RSSetState(raster);
			// Get* calls AddRef the returned objects.
			Release(rtv);
			Release(dsv);
			Release(vs);
			Release(ps);
			for (auto& srv : srvs) {
				Release(srv);
			}
			Release(sampler);
			Release(cbs[0]);
			Release(cbs[1]);
			Release(cbs[2]);
			Release(cbs[3]);
			Release(vertexBuffer);
			Release(indexBuffer);
			Release(layout);
			Release(blend);
			Release(depth);
			Release(raster);
		}
	};

	TextureLab::RenderTarget::~RenderTarget()
	{
		if (shell && originalData) {
			shell->rendererTexture = reinterpret_cast<RE::BSGraphics::Texture*>(originalData);
		}
		Release(rtv);
		Release(srv);
		Release(texture);
		delete ourData;
	}

	namespace
	{
		// The engine's renderer lock: the critical section its render thread
		// holds around its own use of the immediate context. Every pass and
		// readback here runs on the game thread while that thread renders,
		// so each takes the lock for its duration; without it the context is
		// driven from two threads and the driver crashes on a worker thread
		// with nothing of ours on the stack (NOTES 53, 57). The lock is
		// re-entrant, so a pass called from another pass is fine.
		class RendererLock
		{
		public:
			RendererLock() :
				renderer_(RE::BSGraphics::Renderer::GetSingleton())
			{
				if (renderer_) {
					renderer_->Lock();
				}
			}
			~RendererLock()
			{
				if (renderer_) {
					renderer_->Unlock();
				}
			}
			RendererLock(const RendererLock&) = delete;
			RendererLock& operator=(const RendererLock&) = delete;

		private:
			RE::BSGraphics::Renderer* renderer_ = nullptr;
		};
	}

	TextureLab* TextureLab::GetSingleton()
	{
		static TextureLab lab;
		return &lab;
	}

	bool TextureLab::Init()
	{
		if (initTried_) {
			return available_;
		}
		initTried_ = true;

		device_ = RE::BSGraphics::Renderer::GetDevice();
		auto* rendererData = RE::BSGraphics::Renderer::GetRendererData();
		context_ = rendererData ? rendererData->context : nullptr;
		if (!device_ || !context_) {
			logger::error("TextureLab: no D3D11 device/context");
			return false;
		}
		if (!CompileShaders()) {
			return false;
		}

		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.byteWidth = sizeof(Constants);
		cbDesc.usage = D3D11_USAGE_DEFAULT;
		cbDesc.bindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &constants_))) {
			logger::error("TextureLab: constant buffer creation failed");
			return false;
		}
		cbDesc.byteWidth = sizeof(ProgramConstants);
		if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &programConstants_))) {
			logger::error("TextureLab: program constant buffer creation failed");
			return false;
		}
		cbDesc.byteWidth = sizeof(RippleConstants);
		if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &rippleConstants_))) {
			logger::error("TextureLab: ripple constant buffer creation failed");
			return false;
		}
		cbDesc.byteWidth = sizeof(ClassifyConstants);
		if (Failed(device_->CreateBuffer(&cbDesc, nullptr, &classifyConstants_))) {
			logger::error("TextureLab: classify constant buffer creation failed");
			return false;
		}

		D3D11_SAMPLER_DESC sampDesc{};
		sampDesc.filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.addressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.addressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.addressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.maxLOD = D3D11_FLOAT32_MAX;
		if (Failed(device_->CreateSamplerState(&sampDesc, &sampler_))) {
			logger::error("TextureLab: sampler creation failed");
			return false;
		}

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.renderTarget[0].renderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.fillMode = D3D11_FILL_SOLID;
		rasterDesc.cullMode = D3D11_CULL_NONE;
		rasterDesc.depthClipEnable = true;
		if (Failed(device_->CreateBlendState(&blendDesc, &blend_)) ||
			Failed(device_->CreateDepthStencilState(&depthDesc, &depth_)) ||
			Failed(device_->CreateRasterizerState(&rasterDesc, &raster_))) {
			logger::error("TextureLab: pipeline state creation failed");
			return false;
		}

		available_ = true;
		logger::info("TextureLab: ready (runtime layer textures)");
		return true;
	}

	bool TextureLab::CompileShaders()
	{
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		const auto compile = [&](const char* a_entry, const char* a_target) -> ID3DBlob* {
			ID3DBlob* out = nullptr;
			ID3DBlob* err = nullptr;
			const auto hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), Identity::kName.data(), nullptr, nullptr, a_entry, a_target, 0, 0, &out, &err);
			if (Failed(hr)) {
				logger::error("TextureLab: {} compile failed: {}", a_entry, err ? static_cast<const char*>(err->GetBufferPointer()) : "no message");
				Release(err);
				return nullptr;
			}
			Release(err);
			return out;
		};
		(void)code;
		(void)errors;

		auto* vsBlob = compile("VSMain", "vs_5_0");
		auto* psBlob = compile("PSMain", "ps_5_0");
		// The interpreter is separate so a fault in it costs the masks it
		// evaluates, never the layer passes.
		auto* programBlob = compile("PSProgram", "ps_5_0");
		bool  ok = vsBlob && psBlob;
		if (ok) {
			ok = !Failed(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_)) &&
			     !Failed(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_));
			if (!ok) {
				logger::error("TextureLab: shader object creation failed");
			}
		}
		if (ok && programBlob && Failed(device_->CreatePixelShader(programBlob->GetBufferPointer(), programBlob->GetBufferSize(), nullptr, &programPs_))) {
			logger::error("TextureLab: interpreter shader object creation failed; expression masks evaluate as white");
			programPs_ = nullptr;
		}
		if (ok && !programPs_) {
			logger::error("TextureLab: the interpreter pass is unavailable; expression masks evaluate as white");
		}
		auto* rippleBlob = compile("PSRipple", "ps_5_0");
		if (ok && rippleBlob && Failed(device_->CreatePixelShader(rippleBlob->GetBufferPointer(), rippleBlob->GetBufferSize(), nullptr, &ripplePs_))) {
			ripplePs_ = nullptr;
		}
		if (ok && !ripplePs_) {
			logger::error("TextureLab: the ripple pass is unavailable; ripple sources are black");
		}
		Release(rippleBlob);
		auto* classifyBlob = compile("PSClassify", "ps_5_0");
		if (ok && classifyBlob && Failed(device_->CreatePixelShader(classifyBlob->GetBufferPointer(), classifyBlob->GetBufferSize(), nullptr, &classifyPs_))) {
			classifyPs_ = nullptr;
		}
		if (ok && !classifyPs_) {
			logger::error("TextureLab: the classify pass is unavailable; material cluster maps are black");
		}
		Release(classifyBlob);
		// The bake pass is separate for the same reason.
		auto* bakeVsBlob = compile("BakeVS", "vs_5_0");
		auto* bakePsBlob = compile("BakePS", "ps_5_0");
		if (ok && bakeVsBlob && bakePsBlob) {
			const D3D11_INPUT_ELEMENT_DESC elements[2]{
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			if (Failed(device_->CreateVertexShader(bakeVsBlob->GetBufferPointer(), bakeVsBlob->GetBufferSize(), nullptr, &bakeVs_)) ||
				Failed(device_->CreatePixelShader(bakePsBlob->GetBufferPointer(), bakePsBlob->GetBufferSize(), nullptr, &bakePs_)) ||
				Failed(device_->CreateInputLayout(elements, 2, bakeVsBlob->GetBufferPointer(), bakeVsBlob->GetBufferSize(), &bakeLayout_))) {
				logger::error("TextureLab: bake pass objects failed; bakes are unavailable");
				Release(bakeVs_);
				Release(bakePs_);
				Release(bakeLayout_);
			}
		} else if (ok) {
			logger::error("TextureLab: the bake pass did not compile; bakes are unavailable");
		}
		Release(bakeVsBlob);
		Release(bakePsBlob);
		Release(vsBlob);
		Release(psBlob);
		Release(programBlob);
		return ok;
	}

	RE::NiPointer<RE::NiSourceTexture> TextureLab::LoadShell()
	{
		if (nextShell_ >= kShellCount) {
			logger::error("TextureLab: out of shell textures ({})", kShellCount);
			return nullptr;
		}
		const auto                   path = Identity::SlotTexturePath(nextShell_);
		RE::NiPointer<RE::NiTexture> texture;
		RE::BSShaderManager::GetTexture(path.c_str(), true, texture, false);
		auto* source = texture ? netimmerse_cast<RE::NiSourceTexture*>(texture.get()) : nullptr;
		if (!source || !source->rendererTexture) {
			logger::error("TextureLab: shell {} failed to load (is the slots folder installed?)", path);
			return nullptr;
		}
		++nextShell_;
		return RE::NiPointer<RE::NiSourceTexture>{ source };
	}

	bool TextureLab::CreateTarget(RenderTarget& a_target, TextureSize a_size)
	{
		const std::uint32_t  pixels = a_size.Pixels();
		D3D11_TEXTURE2D_DESC desc{};
		desc.width = pixels;
		desc.height = pixels;
		desc.mipLevels = 0;
		desc.arraySize = 1;
		desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.sampleDesc.count = 1;
		desc.usage = D3D11_USAGE_DEFAULT;
		desc.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.miscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (Failed(device_->CreateTexture2D(&desc, nullptr, &a_target.texture))) {
			logger::error("TextureLab: CreateTexture2D({}) failed", pixels);
			return false;
		}
		if (Failed(device_->CreateShaderResourceView(a_target.texture, nullptr, &a_target.srv)) ||
			Failed(device_->CreateRenderTargetView(a_target.texture, nullptr, &a_target.rtv))) {
			logger::error("TextureLab: view creation failed");
			return false;
		}
		a_target.shell = LoadShell();
		if (!a_target.shell) {
			return false;
		}
		a_target.originalData = DataOf(a_target.shell.get());
		a_target.ourData = new RE::NiTexture::RendererData(static_cast<std::uint16_t>(pixels), static_cast<std::uint16_t>(pixels));
		// RendererData is declared against the global forward declarations.
		a_target.ourData->texture = reinterpret_cast<::ID3D11Texture2D*>(a_target.texture);
		a_target.ourData->resourceView = reinterpret_cast<::ID3D11ShaderResourceView*>(a_target.srv);
		a_target.shell->rendererTexture = reinterpret_cast<RE::BSGraphics::Texture*>(a_target.ourData);
		a_target.size = pixels;
		return true;
	}

	std::shared_ptr<TextureLab::RenderTarget> TextureLab::Acquire(TextureSize a_size)
	{
		if (!Init()) {
			return nullptr;
		}
		const auto deleter = [this](RenderTarget* a_target) { Recycle(a_target); };
		for (auto it = pool_.begin(); it != pool_.end(); ++it) {
			if ((*it)->size == a_size.Pixels()) {
				auto* raw = it->release();
				pool_.erase(it);
				return std::shared_ptr<RenderTarget>{ raw, deleter };
			}
		}
		auto target = std::make_unique<RenderTarget>();
		if (!CreateTarget(*target, a_size)) {
			return nullptr;
		}
		return std::shared_ptr<RenderTarget>{ target.release(), deleter };
	}

	TextureLab::RenderTarget* TextureLab::Scratch(TextureSize a_size)
	{
		auto& target = scratch_[a_size.Pixels()];
		if (!target) {
			target = Acquire(a_size);
		}
		return target.get();
	}

	void TextureLab::Recycle(RenderTarget* a_target)
	{
		pool_.emplace_back(a_target);
	}

	void TextureLab::Clear()
	{
		scratch_.clear();
		ClearPreviews();
		pool_.clear();
		luminance_.clear();
		channelMeans_.clear();
		sampleWarned_.clear();
	}

	std::shared_ptr<TextureLab::RenderTarget> TextureLab::Preview(RE::NiSourceTexture* a_source, ShaderChannel a_channel, bool a_dynamic)
	{
		if (!a_source || !available_) {
			return nullptr;
		}
		std::scoped_lock lock{ previewLock_ };
		auto& entry = previews_[{ a_source, a_channel }];
		entry.dynamic = entry.dynamic || a_dynamic;
		entry.wanted = true;
		// Stale or missing: the game thread renders it at its next tick; until
		// then the last picture, if any, is better than none.
		return entry.target;
	}

	void TextureLab::RenderPreviews()
	{
		if (!Init()) {
			return;
		}
		++previewTick_;
		const auto generation = previewGeneration_.load(std::memory_order_relaxed);
		// What to render this tick, taken under the lock; rendering happens
		// outside it so the render thread is never held for a draw.
		std::vector<std::pair<PreviewKey, std::shared_ptr<RenderTarget>>> work;
		{
			std::scoped_lock lock{ previewLock_ };
			if (generation != previewSeen_) {
				// A new generation: entries nobody asked for in the last one go.
				for (auto it = previews_.begin(); it != previews_.end();) {
					if (!it->second.wanted && it->second.generation < previewSeen_) {
						if (it->second.target) {
							previewGraveyard_.emplace_back(std::move(it->second.target), previewTick_);
						}
						it = previews_.erase(it);
					} else {
						++it;
					}
				}
				previewSeen_ = generation;
			}
			for (auto& [key, entry] : previews_) {
				if (!entry.wanted) {
					continue;
				}
				entry.wanted = false;
				const bool stale = !entry.target || entry.generation != generation || entry.dynamic;
				if (!stale) {
					continue;
				}
				if (!entry.target) {
					entry.target = Acquire(TextureSize::Clamp(128));
					if (!entry.target) {
						continue;
					}
				}
				entry.generation = generation;
				work.emplace_back(key, entry.target);
			}
			// A retired target outlives the frame that may still reference it.
			std::erase_if(previewGraveyard_, [&](const auto& a_dead) { return previewTick_ - a_dead.second > 8; });
		}
		for (const auto& [key, target] : work) {
			LayerParams p;
			p.mode = Mode::kChannel;
			p.map = { key.first, MapReading::kRmaos };
			p.channel.channel = key.second;
			Render(*target, nullptr, p);
		}
	}

	void TextureLab::ClearPreviews()
	{
		std::scoped_lock lock{ previewLock_ };
		for (auto& [key, entry] : previews_) {
			if (entry.target) {
				previewGraveyard_.emplace_back(std::move(entry.target), previewTick_);
			}
		}
		previews_.clear();
	}

	bool TextureLab::Render(RenderTarget& a_target, RE::NiSourceTexture* a_source, const LayerParams& a_params)
	{
		const RendererLock rendererLock;
		if (!available_ || !a_target.rtv) {
			return false;
		}
		const bool layerPass = a_params.mode == Mode::kLayer;
		auto*      sourceData = DataOf(layerPass ? a_params.layer.source : a_source);
		if (!layerPass && (!sourceData || !sourceData->resourceView)) {
			sourceData = DataOf(a_params.map.texture);  // channel previews only need the input map
			if (!sourceData || !sourceData->resourceView) {
				return false;
			}
		}
		const bool haveSource = sourceData && sourceData->resourceView;

		const auto& sc = layerPass ? a_params.layer.input.transform : a_params.scroll;
		Constants   constants{};
		constants.offsetScale[0] = sc.uOffset;
		constants.offsetScale[1] = sc.vOffset;
		constants.offsetScale[2] = sc.tileU;
		constants.offsetScale[3] = sc.tileV;
		constants.flags[0] = sc.mirrorU ? 1.0f : 0.0f;
		constants.flags[1] = sc.mirrorV ? 1.0f : 0.0f;
		constants.flags[2] = sc.transpose ? 1.0f : 0.0f;
		constants.flags[3] = static_cast<float>(a_params.mode);
		constants.extra[0] = sc.sourceMip;
		auto*      mapData = DataOf(layerPass ? a_params.layer.mask : a_params.map.texture);
		const bool haveMap = mapData && mapData->resourceView && (layerPass || a_params.map.reading != MapReading::kNone);
		constants.extra[1] = haveMap && !layerPass ? static_cast<float>(a_params.map.reading) : 0.0f;
		auto*      prevData = layerPass ? DataOf(a_params.layer.previous) : nullptr;
		const bool havePrev = prevData && prevData->resourceView;
		if (layerPass) {
			const auto& lp = a_params.layer;
			constants.layer[0] = static_cast<float>(std::to_underlying(lp.input.channel));
			constants.layer[1] = lp.input.meshSpace ? 1.0f : 0.0f;
			constants.layer[2] = static_cast<float>(lp.blend);
			constants.layer[3] = lp.opacity;
			constants.layerColor[0] = lp.color[0];
			constants.layerColor[1] = lp.color[1];
			constants.layerColor[2] = lp.color[2];
			constants.layerColor[3] = lp.normalize;
			constants.layerMask[0] = haveMap ? static_cast<float>(std::to_underlying(lp.maskChannel)) : -1.0f;
			constants.layerMask[1] = static_cast<float>(lp.channels);
			constants.layerMask[2] = havePrev ? 1.0f : 0.0f;
			constants.layerMask[3] = haveSource ? 1.0f : 0.0f;
			constants.layerCurve[0] = lp.curve && lp.curve->srv ? 1.0f : 0.0f;
		}
		// extra.zw carry the mode's two scalars (see the shader's mode branches).
		if (a_params.mode == Mode::kRoughness) {
			constants.extra[2] = a_params.roughness.strength;
			constants.extra[3] = a_params.roughness.contrast;
		} else if (a_params.mode == Mode::kChannel) {
			constants.extra[2] = static_cast<float>(std::to_underlying(a_params.channel.channel));
			constants.extra[3] = a_params.channel.slope ? 1.0f : 0.0f;
		} else if (a_params.mode == Mode::kMaskedGlow) {
			constants.extra[2] = static_cast<float>(a_params.mask.channel);
			constants.extra[3] = a_params.mask.threshold;
			constants.extra2[0] = a_params.mask.softness;
			constants.extra2[1] = a_params.mask.invert ? 1.0f : 0.0f;
			constants.extra2[2] = a_params.mask.strength;
		} else {
			constants.extra[2] = a_params.height.armorWeight;
			constants.extra[3] = a_params.height.noiseWeight;
			constants.extra2[0] = a_params.height.reliefMean;
			constants.extra2[1] = a_params.height.reliefContrast;
			constants.extra2[2] = a_params.height.noiseMean;
		}

		SavedState saved;
		saved.Capture(context_);

		context_->UpdateSubresource(constants_, 0, nullptr, &constants, 0, 0);
		ID3D11RenderTargetView* rtv = a_target.rtv;
		context_->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT viewport{};
		viewport.width = static_cast<float>(a_target.size);
		viewport.height = static_cast<float>(a_target.size);
		viewport.maxDepth = 1.0f;
		context_->RSSetViewports(1, &viewport);
		context_->IASetInputLayout(nullptr);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context_->VSSetShader(vs_, nullptr, 0);
		context_->PSSetShader(ps_, nullptr, 0);
		ID3D11ShaderResourceView* srvs[4]{ haveSource ? reinterpret_cast<ID3D11ShaderResourceView*>(sourceData->resourceView) : nullptr,
			haveMap ? reinterpret_cast<ID3D11ShaderResourceView*>(mapData->resourceView) : nullptr,
			havePrev ? reinterpret_cast<ID3D11ShaderResourceView*>(prevData->resourceView) : nullptr,
			layerPass && a_params.layer.curve ? a_params.layer.curve->srv : nullptr };
		context_->PSSetShaderResources(0, 4, srvs);
		context_->PSSetSamplers(0, 1, &sampler_);
		context_->PSSetConstantBuffers(0, 1, &constants_);
		const float blendFactor[4]{};
		context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(depth_, 0);
		context_->RSSetState(raster_);
		context_->Draw(3, 0);

		// Unbind our target and the armor inputs before the engine binds them.
		ID3D11RenderTargetView*   none = nullptr;
		ID3D11ShaderResourceView* noSrvs[4]{};
		context_->OMSetRenderTargets(1, &none, nullptr);
		context_->PSSetShaderResources(0, 4, noSrvs);
		context_->GenerateMips(a_target.srv);

		saved.Restore(context_);
		return true;
	}

	// Reads the 1x1 mip of a target back through a staging copy.
	std::optional<float> TextureLab::ReadBackMean(RenderTarget& a_target)
	{
		const RendererLock rendererLock;
		std::optional<float> result;
		D3D11_TEXTURE2D_DESC desc{};
		a_target.texture->GetDesc(&desc);
		const auto           lastMip = desc.mipLevels - 1;
		D3D11_TEXTURE2D_DESC stagingDesc{};
		stagingDesc.width = 1;
		stagingDesc.height = 1;
		stagingDesc.mipLevels = 1;
		stagingDesc.arraySize = 1;
		stagingDesc.format = desc.format;
		stagingDesc.sampleDesc.count = 1;
		stagingDesc.usage = D3D11_USAGE_STAGING;
		stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* staging = nullptr;
		if (!Failed(device_->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
			context_->CopySubresourceRegion(staging, 0, 0, 0, 0, a_target.texture, lastMip, nullptr);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (!Failed(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
				const auto* px = static_cast<const std::uint8_t*>(mapped.data);
				result = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
				context_->Unmap(staging, 0);
			} else {
				logger::warn("TextureLab: staging map failed; mean readback unavailable");
			}
			Release(staging);
		} else {
			logger::warn("TextureLab: staging texture creation failed; mean readback unavailable");
		}
		return result;
	}

	std::vector<std::uint8_t> TextureLab::ReadBackPixels(RenderTarget& a_target)
	{
		const RendererLock rendererLock;
		std::vector<std::uint8_t> out;
		if (!a_target.texture) {
			return out;
		}
		D3D11_TEXTURE2D_DESC desc{};
		a_target.texture->GetDesc(&desc);
		// The bytes are read as RGBA8, so any other format is refused before the map.
		if (desc.format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.width == 0 || desc.height == 0 || desc.width != a_target.size || desc.height != a_target.size) {
			logger::warn("TextureLab: pixel readback refused: target {}x{} format {}", desc.width, desc.height, static_cast<std::uint32_t>(desc.format));
			return out;
		}
		D3D11_TEXTURE2D_DESC stagingDesc{};
		stagingDesc.width = desc.width;
		stagingDesc.height = desc.height;
		stagingDesc.mipLevels = 1;
		stagingDesc.arraySize = 1;
		stagingDesc.format = desc.format;
		stagingDesc.sampleDesc.count = 1;
		stagingDesc.usage = D3D11_USAGE_STAGING;
		stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* staging = nullptr;
		if (Failed(device_->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
			logger::warn("TextureLab: staging texture creation failed; pixel readback unavailable");
			return out;
		}
		context_->CopySubresourceRegion(staging, 0, 0, 0, 0, a_target.texture, 0, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (!Failed(context_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)) && mapped.data) {
			const std::size_t rowBytes = static_cast<std::size_t>(desc.width) * 4;
			if (mapped.rowPitch >= rowBytes) {
				out.resize(rowBytes * desc.height);
				const auto* rows = static_cast<const std::uint8_t*>(mapped.data);
				for (std::uint32_t y = 0; y < desc.height; ++y) {
					std::memcpy(out.data() + y * rowBytes, rows + static_cast<std::size_t>(y) * mapped.rowPitch, rowBytes);
				}
			}
			context_->Unmap(staging, 0);
		} else {
			logger::warn("TextureLab: staging map failed; pixel readback unavailable");
		}
		Release(staging);
		return out;
	}

	namespace
	{
		// The mip of a map whose side is still at least a_side: sampling it at
		// a_side points reads whole texels of a mip average, not a sparse pick.
		float MipThatFits(const TextureLab::Extent& a_extent, std::uint32_t a_side) noexcept
		{
			std::uint32_t side = (std::max)(a_extent.width, a_extent.height);
			std::uint32_t mip = 0;
			while (mip < 16 && (side >> (mip + 1)) >= a_side) {
				++mip;
			}
			return static_cast<float>(mip);
		}
	}

	std::optional<MaterialSample> TextureLab::SampleMaterial(RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse)
	{
		static_assert(kSampleSide * kSampleSide <= kMaxSampleTexels);
		const auto fail = [&](RE::NiSourceTexture* a_texture, std::string_view a_why) -> std::optional<MaterialSample> {
			if (sampleWarned_.insert(a_texture).second) {
				logger::warn("TextureLab: material sample: {}", a_why);
			}
			return std::nullopt;
		};
		if (!Init()) {
			return fail(nullptr, "the lab is unavailable");
		}
		const auto rmaosExtent = ExtentOf(a_rmaos);
		if (!rmaosExtent) {
			return fail(a_rmaos, "the RMAOS map is null or not a resident 2D texture");
		}
		const auto diffuseExtent = ExtentOf(a_diffuse);
		if (!diffuseExtent) {
			return fail(a_diffuse, "the diffuse map is null or not a resident 2D texture");
		}
		auto target = Acquire(TextureSize::Clamp(kSampleSide));
		if (!target || target->size != kSampleSide) {
			return fail(a_rmaos, "no sample target");
		}
		// Each map copied at its fitting mip into the target, then read back.
		const auto copyBack = [&](RE::NiSourceTexture* a_map, const Extent& a_extent) -> std::vector<std::uint8_t> {
			LayerParams params;
			params.mode = Mode::kCopy;
			params.scroll.sourceMip = MipThatFits(a_extent, kSampleSide);
			if (!Render(*target, a_map, params)) {
				return {};
			}
			return ReadBackPixels(*target);
		};
		const std::size_t texelCount = static_cast<std::size_t>(kSampleSide) * kSampleSide;
		const auto        rmaos = copyBack(a_rmaos, *rmaosExtent);
		if (rmaos.size() != texelCount * 4) {
			return fail(a_rmaos, "the RMAOS map could not be read back");
		}
		const auto diffuse = copyBack(a_diffuse, *diffuseExtent);
		if (diffuse.size() != texelCount * 4) {
			return fail(a_diffuse, "the diffuse map could not be read back");
		}
		MaterialSample sample;
		sample.width = kSampleSide;
		sample.height = kSampleSide;
		sample.texels.reserve(texelCount);
		constexpr float scale = 1.0f / 255.0f;
		for (std::size_t i = 0; i < texelCount; ++i) {
			const std::uint8_t* m = rmaos.data() + i * 4;
			const std::uint8_t* d = diffuse.data() + i * 4;
			Texel texel;
			texel.roughness = m[0] * scale;
			texel.metallic = m[1] * scale;
			texel.occlusion = m[2] * scale;
			texel.reflectance = m[3] * scale;
			texel.luma = (0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2]) * scale;
			sample.texels.push_back(texel);
		}
		return sample;
	}

	bool TextureLab::RenderClusters(RenderTarget& a_target, RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse, const MaterialAnalysis& a_analysis)
	{
		const RendererLock rendererLock;
		const auto* rmaosData = DataOf(a_rmaos);
		const auto* diffuseData = DataOf(a_diffuse);
		if (!available_ || !a_target.rtv || !classifyPs_ || !rmaosData || !rmaosData->resourceView || !diffuseData || !diffuseData->resourceView ||
			a_analysis.clusters.size() > kMaxClusters) {
			return false;
		}
		// A weight that is not finite or not positive counts as zero, as ScalesOf does on the CPU.
		const auto scale = [](float a_weight) { return std::isfinite(a_weight) && a_weight > 0.0f ? a_weight : 0.0f; };
		ClassifyConstants constants{};
		for (std::size_t k = 0; k < a_analysis.clusters.size(); ++k) {
			const auto& cluster = a_analysis.clusters[k];
			constants.centroidRmaos[k][0] = cluster.centroid.roughness;
			constants.centroidRmaos[k][1] = cluster.centroid.metallic;
			constants.centroidRmaos[k][2] = cluster.centroid.occlusion;
			constants.centroidRmaos[k][3] = cluster.centroid.reflectance;
			constants.centroidLuma[k][0] = cluster.centroid.luma;
			constants.centroidLuma[k][1] = static_cast<float>(cluster.id);
		}
		const auto& w = a_analysis.settings.weights;
		constants.weights[0] = scale(w.roughness);
		constants.weights[1] = scale(w.metallic);
		constants.weights[2] = scale(w.occlusion);
		constants.weights[3] = scale(w.reflectance);
		constants.misc[0] = scale(w.luma);
		constants.misc[1] = static_cast<float>(a_analysis.clusters.size());

		SavedState saved;
		saved.Capture(context_);
		context_->UpdateSubresource(classifyConstants_, 0, nullptr, &constants, 0, 0);
		ID3D11RenderTargetView* rtv = a_target.rtv;
		context_->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT viewport{};
		viewport.width = static_cast<float>(a_target.size);
		viewport.height = static_cast<float>(a_target.size);
		viewport.maxDepth = 1.0f;
		context_->RSSetViewports(1, &viewport);
		context_->IASetInputLayout(nullptr);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context_->VSSetShader(vs_, nullptr, 0);
		context_->PSSetShader(classifyPs_, nullptr, 0);
		ID3D11ShaderResourceView* srvs[12]{ reinterpret_cast<ID3D11ShaderResourceView*>(diffuseData->resourceView),
			reinterpret_cast<ID3D11ShaderResourceView*>(rmaosData->resourceView) };
		context_->PSSetShaderResources(0, 12, srvs);
		context_->PSSetSamplers(0, 1, &sampler_);
		ID3D11Buffer* cbs[4]{ constants_, programConstants_, rippleConstants_, classifyConstants_ };
		context_->PSSetConstantBuffers(0, 4, cbs);
		const float blendFactor[4]{};
		context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(depth_, 0);
		context_->RSSetState(raster_);
		context_->Draw(3, 0);
		ID3D11RenderTargetView*   none = nullptr;
		ID3D11ShaderResourceView* noSrvs[12]{};
		context_->OMSetRenderTargets(1, &none, nullptr);
		context_->PSSetShaderResources(0, 12, noSrvs);
		context_->GenerateMips(a_target.srv);
		saved.Restore(context_);
		return true;
	}

	TextureLab::Lookup::~Lookup()
	{
		Release(srv);
		Release(texture);
	}

	bool TextureLab::RenderProgram(RenderTarget& a_target, const ProgramPass& a_pass)
	{
		const RendererLock rendererLock;
		// The counts are checked against the arrays, not trusted: a pass that
		// claims more than it holds is refused here rather than read past.
		if (!available_ || !a_target.rtv || !programPs_ || a_pass.code.size() > 256 || a_pass.refCount > a_pass.refs.size() ||
			a_pass.textureCount > a_pass.textures.size() || a_pass.curveCount > a_pass.curves.size()) {
			return false;
		}
		auto constants = std::make_unique<ProgramConstants>();
		std::memset(constants.get(), 0, sizeof(ProgramConstants));
		for (std::size_t k = 0; k < a_pass.code.size(); ++k) {
			constants->code[k][0] = static_cast<float>(a_pass.code[k].op);
			constants->code[k][1] = a_pass.code[k].number;
			constants->code[k][2] = static_cast<float>(a_pass.code[k].index);
		}
		for (std::size_t r = 0; r < a_pass.refCount; ++r) {
			const auto& ref = a_pass.refs[r];
			constants->refs[r][0] = ref.isTexture ? 1.0f : 0.0f;
			constants->refs[r][1] = static_cast<float>(ref.texture);
			constants->refValues[r][0] = ref.value.x;
			constants->refValues[r][1] = ref.value.y;
			constants->refValues[r][2] = ref.value.z;
		}
		ID3D11ShaderResourceView* srvs[12]{};
		for (std::size_t t = 0; t < a_pass.textureCount; ++t) {
			const auto& tex = a_pass.textures[t];
			const auto* data = DataOf(tex.texture);
			srvs[t] = data ? reinterpret_cast<ID3D11ShaderResourceView*>(data->resourceView) : nullptr;
			const auto& sc = tex.sampling.transform;
			constants->texParams[t][0] = static_cast<float>(std::to_underlying(tex.sampling.channel));
			constants->texParams[t][1] = tex.sampling.meshSpace ? 1.0f : 0.0f;
			constants->texParams[t][2] = tex.normalize;
			constants->texParams[t][3] = sc.sourceMip;
			constants->texTransform[t][0] = sc.uOffset;
			constants->texTransform[t][1] = sc.vOffset;
			constants->texTransform[t][2] = sc.tileU;
			constants->texTransform[t][3] = sc.tileV;
			constants->texFlags[t][0] = sc.mirrorU ? 1.0f : 0.0f;
			constants->texFlags[t][1] = sc.mirrorV ? 1.0f : 0.0f;
			constants->texFlags[t][2] = sc.transpose ? 1.0f : 0.0f;
		}
		for (std::size_t c = 0; c < a_pass.curveCount; ++c) {
			srvs[8 + c] = a_pass.curves[c] ? a_pass.curves[c]->srv : nullptr;
		}
		constants->misc[0] = a_pass.time;
		constants->misc[1] = static_cast<float>(a_pass.code.size());
		constants->misc[2] = a_pass.vectorResult ? 1.0f : 0.0f;

		SavedState saved;
		saved.Capture(context_);
		context_->UpdateSubresource(programConstants_, 0, nullptr, constants.get(), 0, 0);
		ID3D11RenderTargetView* rtv = a_target.rtv;
		context_->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT viewport{};
		viewport.width = static_cast<float>(a_target.size);
		viewport.height = static_cast<float>(a_target.size);
		viewport.maxDepth = 1.0f;
		context_->RSSetViewports(1, &viewport);
		context_->IASetInputLayout(nullptr);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context_->VSSetShader(vs_, nullptr, 0);
		context_->PSSetShader(programPs_, nullptr, 0);
		context_->PSSetShaderResources(0, 12, srvs);
		context_->PSSetSamplers(0, 1, &sampler_);
		ID3D11Buffer* cbs[2]{ constants_, programConstants_ };
		context_->PSSetConstantBuffers(0, 2, cbs);
		const float blendFactor[4]{};
		context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(depth_, 0);
		context_->RSSetState(raster_);
		context_->Draw(3, 0);
		ID3D11RenderTargetView*   none = nullptr;
		ID3D11ShaderResourceView* noSrvs[12]{};
		context_->OMSetRenderTargets(1, &none, nullptr);
		context_->PSSetShaderResources(0, 12, noSrvs);
		context_->GenerateMips(a_target.srv);
		saved.Restore(context_);
		return true;
	}

	std::vector<std::uint8_t> TextureLab::ReadBuffer(REX::W32::ID3D11Buffer* a_buffer, std::uint32_t a_bytes)
	{
		const RendererLock rendererLock;
		std::vector<std::uint8_t> out;
		if (!a_buffer || a_bytes == 0 || !Init()) {
			return out;
		}
		D3D11_BUFFER_DESC desc{};
		desc.byteWidth = a_bytes;
		desc.usage = D3D11_USAGE_STAGING;
		desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
		REX::W32::ID3D11Buffer* staging = nullptr;
		if (Failed(device_->CreateBuffer(&desc, nullptr, &staging))) {
			return out;
		}
		// A region copy, because the source is longer than the bytes wanted.
		const D3D11_BOX box{ 0, 0, 0, a_bytes, 1, 1 };
		context_->CopySubresourceRegion(reinterpret_cast<REX::W32::ID3D11Resource*>(staging), 0, 0, 0, 0, reinterpret_cast<REX::W32::ID3D11Resource*>(a_buffer), 0, &box);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (!Failed(context_->Map(reinterpret_cast<REX::W32::ID3D11Resource*>(staging), 0, D3D11_MAP_READ, 0, &mapped)) && mapped.data) {
			out.assign(static_cast<const std::uint8_t*>(mapped.data), static_cast<const std::uint8_t*>(mapped.data) + a_bytes);
			context_->Unmap(reinterpret_cast<REX::W32::ID3D11Resource*>(staging), 0);
		}
		Release(staging);
		return out;
	}

	bool TextureLab::BakeMesh(RenderTarget& a_target, const BakeBuffers& a_bake)
	{
		const RendererLock rendererLock;
		if (!available_ || !a_target.rtv || !BakingAvailable() || a_bake.vertices.empty() || a_bake.indices.empty()) {
			return false;
		}
		D3D11_BUFFER_DESC vbDesc{};
		vbDesc.byteWidth = static_cast<std::uint32_t>(a_bake.vertices.size() * sizeof(BakeVertex));
		vbDesc.usage = D3D11_USAGE_IMMUTABLE;
		vbDesc.bindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_BUFFER_DESC ibDesc{};
		ibDesc.byteWidth = static_cast<std::uint32_t>(a_bake.indices.size() * sizeof(std::uint32_t));
		ibDesc.usage = D3D11_USAGE_IMMUTABLE;
		ibDesc.bindFlags = D3D11_BIND_INDEX_BUFFER;
		REX::W32::D3D11_SUBRESOURCE_DATA vbData{ a_bake.vertices.data(), 0, 0 };
		REX::W32::D3D11_SUBRESOURCE_DATA ibData{ a_bake.indices.data(), 0, 0 };
		REX::W32::ID3D11Buffer*          vb = nullptr;
		REX::W32::ID3D11Buffer*          ib = nullptr;
		if (Failed(device_->CreateBuffer(&vbDesc, &vbData, &vb)) || Failed(device_->CreateBuffer(&ibDesc, &ibData, &ib))) {
			Release(vb);
			Release(ib);
			logger::error("TextureLab: bake buffers could not be created");
			return false;
		}

		SavedState saved;
		saved.Capture(context_);
		ID3D11RenderTargetView* rtv = a_target.rtv;
		const float             black[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		context_->ClearRenderTargetView(rtv, black);
		context_->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT viewport{};
		viewport.width = static_cast<float>(a_target.size);
		viewport.height = static_cast<float>(a_target.size);
		viewport.maxDepth = 1.0f;
		context_->RSSetViewports(1, &viewport);
		const std::uint32_t stride = sizeof(BakeVertex);
		const std::uint32_t offset = 0;
		context_->IASetInputLayout(bakeLayout_);
		context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context_->VSSetShader(bakeVs_, nullptr, 0);
		context_->PSSetShader(bakePs_, nullptr, 0);
		ID3D11ShaderResourceView* noSrvs[12]{};
		context_->PSSetShaderResources(0, 12, noSrvs);
		const float blendFactor[4]{};
		context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(depth_, 0);
		context_->RSSetState(raster_);
		context_->DrawIndexed(static_cast<std::uint32_t>(a_bake.indices.size()), 0, 0);
		ID3D11RenderTargetView* none = nullptr;
		context_->OMSetRenderTargets(1, &none, nullptr);
		context_->GenerateMips(a_target.srv);
		saved.Restore(context_);
		Release(vb);
		Release(ib);
		return true;
	}

	bool TextureLab::RenderRipple(RenderTarget& a_target, const RipplePass& a_pass)
	{
		const RendererLock rendererLock;
		const auto* positions = DataOf(a_pass.positions);
		if (!available_ || !a_target.rtv || !ripplePs_ || !positions || !positions->resourceView) {
			return false;
		}
		RippleConstants constants{};
		const auto      count = std::min<std::size_t>(a_pass.firingCount, a_pass.firings.size());
		for (std::size_t k = 0; k < count; ++k) {
			constants.firings[k][0] = a_pass.firings[k].origin.x;
			constants.firings[k][1] = a_pass.firings[k].origin.y;
			constants.firings[k][2] = a_pass.firings[k].origin.z;
			constants.firings[k][3] = a_pass.firings[k].age;
		}
		constants.shape[0] = a_pass.speed;
		constants.shape[1] = a_pass.width;
		constants.shape[2] = a_pass.decay;
		constants.shape[3] = a_pass.disc ? 1.0f : 0.0f;
		constants.misc[0] = static_cast<float>(count);
		constants.misc[1] = a_pass.frame;

		SavedState saved;
		saved.Capture(context_);
		context_->UpdateSubresource(rippleConstants_, 0, nullptr, &constants, 0, 0);
		ID3D11RenderTargetView* rtv = a_target.rtv;
		context_->OMSetRenderTargets(1, &rtv, nullptr);
		D3D11_VIEWPORT viewport{};
		viewport.width = static_cast<float>(a_target.size);
		viewport.height = static_cast<float>(a_target.size);
		viewport.maxDepth = 1.0f;
		context_->RSSetViewports(1, &viewport);
		context_->IASetInputLayout(nullptr);
		context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context_->VSSetShader(vs_, nullptr, 0);
		context_->PSSetShader(ripplePs_, nullptr, 0);
		ID3D11ShaderResourceView* srvs[12]{ reinterpret_cast<ID3D11ShaderResourceView*>(positions->resourceView) };
		context_->PSSetShaderResources(0, 12, srvs);
		context_->PSSetSamplers(0, 1, &sampler_);
		ID3D11Buffer* cbs[3]{ constants_, programConstants_, rippleConstants_ };
		context_->PSSetConstantBuffers(0, 3, cbs);
		const float blendFactor[4]{};
		context_->OMSetBlendState(blend_, blendFactor, 0xFFFFFFFF);
		context_->OMSetDepthStencilState(depth_, 0);
		context_->RSSetState(raster_);
		context_->Draw(3, 0);
		ID3D11RenderTargetView*   none = nullptr;
		ID3D11ShaderResourceView* noSrvs[12]{};
		context_->OMSetRenderTargets(1, &none, nullptr);
		context_->PSSetShaderResources(0, 12, noSrvs);
		context_->GenerateMips(a_target.srv);
		saved.Restore(context_);
		return true;
	}

	std::shared_ptr<TextureLab::Lookup> TextureLab::CreateLookup(std::span<const float, 256> a_values)
	{
		if (!Init()) {
			return nullptr;
		}
		auto lookup = std::make_shared<Lookup>();
		D3D11_TEXTURE2D_DESC desc{};
		desc.width = 256;
		desc.height = 1;
		desc.mipLevels = 1;
		desc.arraySize = 1;
		desc.format = DXGI_FORMAT_R32_FLOAT;
		desc.sampleDesc.count = 1;
		desc.usage = D3D11_USAGE_IMMUTABLE;
		desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
		REX::W32::D3D11_SUBRESOURCE_DATA data{ a_values.data(), 256 * sizeof(float), 0 };
		if (Failed(device_->CreateTexture2D(&desc, &data, &lookup->texture)) || Failed(device_->CreateShaderResourceView(lookup->texture, nullptr, &lookup->srv))) {
			logger::error("TextureLab: could not create a curve lookup");
			return nullptr;
		}
		return lookup;
	}

	std::optional<TextureLab::Extent> TextureLab::ExtentOf(RE::NiSourceTexture* a_source)
	{
		const auto* data = DataOf(a_source);
		if (!data || !data->resourceView) {
			return std::nullopt;
		}
		auto* srv = reinterpret_cast<REX::W32::ID3D11ShaderResourceView*>(data->resourceView);
		REX::W32::ID3D11Resource* resource = nullptr;
		srv->GetResource(&resource);
		if (!resource) {
			return std::nullopt;
		}
		REX::W32::ID3D11Texture2D* texture = nullptr;
		resource->QueryInterface(REX::W32::IID_ID3D11Texture2D, reinterpret_cast<void**>(&texture));
		std::optional<Extent> extent;
		if (texture) {
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			extent = Extent{ desc.width, desc.height };
		}
		Release(texture);
		Release(resource);
		return extent;
	}

	float TextureLab::MeanLuminance(RE::NiSourceTexture* a_source)
	{
		if (const auto it = luminance_.find(a_source); it != luminance_.end()) {
			return it->second;
		}
		float result = 0.5f;
		auto  target = Acquire(TextureSize::Clamp(64));
		if (target && Render(*target, a_source, LayerParams{})) {
			result = ReadBackMean(*target).value_or(0.5f);
		} else {
			logger::warn("TextureLab: mean luminance render failed; using 0.5");
		}
		luminance_[a_source] = result;
		return result;
	}

	float TextureLab::MeanChannel(RE::NiSourceTexture* a_source, ShaderChannel a_channel)
	{
		const auto key = std::make_pair(a_source, a_channel);
		if (const auto it = channelMeans_.find(key); it != channelMeans_.end()) {
			return it->second;
		}
		float result = 0.5f;
		auto  target = Acquire(TextureSize::Clamp(64));
		if (target) {
			LayerParams p;
			p.mode = Mode::kChannel;
			p.map = { a_source, MapReading::kRmaos };
			p.channel.channel = a_channel;
			if (Render(*target, nullptr, p)) {
				result = ReadBackMean(*target).value_or(0.5f);  // grey output, so luminance is the channel mean
			} else {
				logger::warn("TextureLab: channel mean render failed; using 0.5");
			}
		} else {
			logger::warn("TextureLab: no target for channel mean; using 0.5");
		}
		channelMeans_[key] = result;
		return result;
	}
}
