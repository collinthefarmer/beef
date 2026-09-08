#include "Timing.h"

#include <cmath>
#include <cstdio>

using namespace WornEnchantmentPBR::Timing;

namespace
{
	int failures = 0;

	bool Near(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	void Check(bool ok, const char* what)
	{
		if (!ok) {
			++failures;
			std::printf("FAIL: %s\n", what);
		}
	}

	EffectParams ThreeKeyParams()
	{
		EffectParams p{};
		p.colorKeys = { Rgb{ 1, 0, 0 }, Rgb{ 0, 1, 0 }, Rgb{ 0, 0, 1 } };
		p.colorKeyTimes = { 1.0f, 2.0f, 4.0f };
		p.colorKeyScales = { 1.0f, 2.0f, 4.0f };
		p.colorScale = 1.0f;
		p.fill.fullAlphaRatio = 1.0f;
		p.fill.persistentAlphaRatio = 1.0f;
		return p;
	}
}

int main()
{
	Check(Near(ClampAnimationSpeed(10.0f), 4.0f), "speed clamps to 4.0");
	Check(Near(ClampAnimationSpeed(0.0f), 0.05f), "speed clamps to 0.05");
	Check(Near(ClampIntensity(5.0f), 2.0f), "intensity clamps to 2.0");
	Check(Near(ClampIntensity(0.0f), 0.1f), "intensity clamps to 0.1");

	Check(Near(SegmentAmount(1.5f, 1.0f, 2.0f), 0.5f), "segment midpoint");
	Check(Near(SegmentAmount(0.0f, 1.0f, 2.0f), 0.0f), "segment below start clamps to 0");
	Check(Near(SegmentAmount(9.0f, 1.0f, 2.0f), 1.0f), "segment past end clamps to 1");
	Check(Near(SegmentAmount(1.0f, 1.0f, 1.0f), 1.0f), "degenerate segment is 1");

	{
		const auto p = ThreeKeyParams();
		auto s = Evaluate(p, 0.0f, 1.0f, 1.0f);
		Check(Near(s.color.r, 1) && Near(s.color.g, 0), "t=0 is key 1");
		Check(Near(s.scale, 1.0f), "t=0 scale is key 1 scale");

		s = Evaluate(p, 1.0f, 1.0f, 1.0f);
		Check(Near(s.color.r, 1) && Near(s.color.g, 0), "t=key1 time is still key 1 (segment starts strictly after)");

		s = Evaluate(p, 1.5f, 1.0f, 1.0f);
		Check(Near(s.color.r, 0.5f) && Near(s.color.g, 0.5f) && Near(s.color.b, 0), "t=1.5 halfway key1->key2");
		Check(Near(s.scale, 1.5f), "t=1.5 scale halfway 1->2");

		s = Evaluate(p, 2.0f, 1.0f, 1.0f);
		Check(Near(s.color.g, 1) && Near(s.color.r, 0), "t=key2 time is key 2");

		s = Evaluate(p, 3.0f, 1.0f, 1.0f);
		Check(Near(s.color.g, 0.5f) && Near(s.color.b, 0.5f), "t=3 halfway key2->key3");
		Check(Near(s.scale, 3.0f), "t=3 scale halfway 2->4");

		s = Evaluate(p, 4.0f, 1.0f, 1.0f);
		Check(Near(s.color.r, 1) && Near(s.color.g, 0), "t=key3 time wraps to key 1");

		s = Evaluate(p, 0.75f, 2.0f, 1.0f);
		Check(Near(s.color.r, 0.5f) && Near(s.color.g, 0.5f), "speed scales phase");
	}

	{
		auto p = ThreeKeyParams();
		p.colorKeyTimes = { 0, 0, 0 };
		p.colorScale = 0.5f;
		const auto s = Evaluate(p, 7.0f, 1.0f, 1.0f);
		Check(Near(s.color.r, 1) && Near(s.color.g, 0), "no key times keeps key 1");
		Check(Near(s.scale, 0.5f), "scale = colorScale * key1 scale");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 1.0f;
		a.persistentAlphaRatio = 0.4f;
		a.pulseAmplitude = 0.0f;
		a.pulseFrequency = 2.0f;
		Check(Near(EvaluateAlpha(a, 0.0f, 1.0f), 0.4f), "zero amplitude at t=0");
		Check(Near(EvaluateAlpha(a, 0.37f, 1.0f), 0.4f), "zero amplitude at t=0.37");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 1.0f;
		a.persistentAlphaRatio = 0.4f;
		a.pulseAmplitude = 1.0f;
		a.pulseFrequency = 1.0f;
		Check(Near(EvaluateAlpha(a, 0.0f, 1.0f), 0.4f), "pulse trough at t=0");
		Check(Near(EvaluateAlpha(a, 0.5f, 1.0f), 1.0f), "pulse peak at half period");
		a.pulseAmplitude = 0.5f;
		Check(Near(EvaluateAlpha(a, 0.5f, 1.0f), 0.7f), "half amplitude peaks halfway");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 0.8f;
		a.persistentAlphaRatio = 0.0f;
		Check(Near(EvaluateAlpha(a, 1.0f, 1.0f), 0.8f), "persistent 0 uses full ratio");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 1.0f;
		a.persistentAlphaRatio = 1.0f;
		a.fadeInTime = 2.0f;
		Check(Near(EvaluateAlpha(a, 0.0f, 1.0f), 0.0f), "fade-in is 0 at t=0");
		Check(Near(EvaluateAlpha(a, 1.0f, 1.0f), 0.5f), "fade-in is 0.5 at half");
		Check(Near(EvaluateAlpha(a, 2.0f, 1.0f), 1.0f), "fade-in is 1 at t=fadeIn");
		Check(Near(EvaluateAlpha(a, 5.0f, 1.0f), 1.0f), "fade-in is 1 after");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 0.5f;
		a.persistentAlphaRatio = 0.5f;
		Check(Near(EvaluateAlpha(a, 1.0f, 2.0f), 1.0f), "intensity 2 saturates 0.5 to 1");
		Check(Near(EvaluateAlpha(a, 1.0f, 0.5f), 0.25f), "intensity 0.5 halves");
	}

	{
		auto p = ThreeKeyParams();
		p.animationSpeedU = 0.25f;
		p.animationSpeedV = -0.25f;
		const auto s = Evaluate(p, 6.0f, 1.0f, 1.0f);
		Check(Near(s.uOffset, 0.5f), "u offset wraps forward");
		Check(Near(s.vOffset, 0.5f), "v offset wraps negative into [0,1)");
	}

	Check(FrameIndex(0.0f, 0.0f, true, false, 16) == 0, "frame 0 at offset 0");
	Check(FrameIndex(0.5f, 0.0f, true, false, 16) == 8, "frame 8 at u=0.5");
	Check(FrameIndex(0.999f, 0.0f, true, false, 16) == 15, "last frame near 1");
	Check(FrameIndex(0.0f, 0.25f, false, true, 16) == 4, "v-only scroll uses v");
	Check(FrameIndex(0.5f, 0.25f, true, true, 16) == 8, "both axes use u");
	Check(FrameIndex(0.5f, 0.0f, true, false, 0) == 0, "zero frames yields 0");

	{
		auto p = ThreeKeyParams();
		p.edgeColor = Rgb{ 0.1f, 0.2f, 0.5f };
		p.edge.fullAlphaRatio = 0.9f;
		p.edge.persistentAlphaRatio = 0.9f;
		const auto s = Evaluate(p, 3.0f, 1.0f, 1.0f);
		Check(Near(s.edgeAlpha, 0.9f), "edge alpha is the persistent ratio");
		Check(Near(s.edgeColor.b, 0.5f), "edge colour passes through");
	}

	{
		ColorPolicy policy{};
		Check(Near(Chroma(Rgb{ 0.4f, 0.4f, 0.4f }), 0.0f), "grey has no chroma");
		Check(Near(Chroma(Rgb{ 0.0f, 0.0f, 0.0f }), 0.0f), "black has no chroma");
		Check(Chroma(Rgb{ 0.08f, 0.2f, 0.5f }) > 0.5f, "blue has chroma");

		auto c = ResolveEmissiveColor(Rgb{ 0.4f, 0.4f, 0.4f }, Rgb{ 0.08f, 0.2f, 0.5f }, policy);
		Check(Near(c.b, 0.4f) && Near(c.r, 0.064f), "grey fill takes the edge hue at fill brightness");

		c = ResolveEmissiveColor(Rgb{ 0, 0, 0 }, Rgb{ 0.12f, 0.31f, 0.15f }, policy);
		Check(Near(c.g, 1.0f) && c.r < 0.4f, "black fill becomes white then tinted");

		c = ResolveEmissiveColor(Rgb{ 0.18f, 0.18f, 0.18f }, Rgb{ 1, 1, 1 }, policy);
		Check(Near(c.r, 0.18f) && Near(c.g, 0.18f), "white edge leaves the fill untouched");

		policy.override = Rgb{ 1.0f, 0.0f, 0.0f };
		c = ResolveEmissiveColor(Rgb{ 0.4f, 0.4f, 0.4f }, Rgb{ 0.08f, 0.2f, 0.5f }, policy);
		Check(Near(c.r, 0.4f) && Near(c.g, 0.0f), "override sets the hue");

		policy = ColorPolicy{};
		policy.tintWithEdge = false;
		c = ResolveEmissiveColor(Rgb{ 0, 0, 0 }, Rgb{ 0.12f, 0.31f, 0.15f }, policy);
		Check(Near(c.r, 1.0f) && Near(c.g, 1.0f), "no tint: black fill lifts to white");
		policy.blackFillAsWhite = false;
		c = ResolveEmissiveColor(Rgb{ 0, 0, 0 }, Rgb{ 0.12f, 0.31f, 0.15f }, policy);
		Check(Near(c.r, 0.0f) && Near(c.g, 0.0f), "no lift: black stays black");
	}

	{
		AlphaParams a{};
		a.fullAlphaRatio = 0.9f;
		a.persistentAlphaRatio = 0.4f;
		Check(Near(BaselineAlpha(a), 0.4f), "baseline is persistent when set");
		a.persistentAlphaRatio = 0.0f;
		Check(Near(BaselineAlpha(a), 0.9f), "baseline falls back to full");
		a.persistentAlphaRatio = 0.4f;
		a.pulseAmplitude = 1.0f;
		a.pulseFrequency = 1.0f;
		Check(Near(EvaluateAlpha(a, 0.0f, 1.0f) / BaselineAlpha(a), 1.0f), "normalised trough is 1");
		Check(Near(EvaluateAlpha(a, 0.5f, 1.0f) / BaselineAlpha(a), 2.25f), "normalised peak is full/persistent");
		const auto n = NormalizeHue(Rgb{ 0.1f, 0.2f, 0.5f });
		Check(Near(n.b, 1.0f) && Near(n.r, 0.2f), "hue normalises to max 1");
		Check(Near(NormalizeHue(Rgb{}).r, 0.0f), "black stays black");
	}

	if (failures == 0) {
		std::printf("all timing checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
