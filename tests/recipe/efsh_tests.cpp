#include "recipe/Efsh.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace
{
	bool NearVec(const Vec3& a_got, const Vec3& a_want)
	{
		return Near(a_got.x, a_want.x) && Near(a_got.y, a_want.y) && Near(a_got.z, a_want.z);
	}

	Efsh::EffectParams ThreeKeyGradient()
	{
		Efsh::EffectParams params;
		params.colorKeys = { Vec3{ 1.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f } };
		params.colorKeyTimes = { 0.0f, 1.0f, 2.0f };
		params.colorKeyScales = { 1.0f, 2.0f, 4.0f };
		return params;
	}
}

int main()
{
	Efsh::EffectParams params;
	params.fill.fullAlphaRatio = 0.5f;
	params.colorKeys[0] = Vec3{ 0.2f, 0.4f, 1.0f };
	Check(params.colorScale == 1.0f, "the effect params default their colour scale");
	Check(params.colorKeys[0] == Vec3{ 0.2f, 0.4f, 1.0f }, "a colour key holds a Vec3");

	Efsh::FillState state;
	Check(state.alpha == 1.0f, "the fill state defaults a full alpha");
	Check(state.color == Vec3{}, "the fill state defaults a black colour");

	Check(Near(Efsh::BaselineAlpha(Efsh::AlphaParams{}), 1.0f), "a full-persistent alpha params baselines at one");
	Check(Near(Efsh::BaselineAlpha(Efsh::AlphaParams{ 0.5f, 0.0f }), 0.5f), "a zero persistent baselines to the full ratio");
	Check(Near(Efsh::BaselineAlpha(Efsh::AlphaParams{ 0.0f, 0.0f }), 1e-4f), "an all-zero alpha params baselines to the floor");

	{
		const Efsh::FillState d = Efsh::Evaluate(Efsh::EffectParams{}, 2.0f, 1.0f, 1.0f);
		Check(NearVec(d.color, Vec3{}), "default params hold the first key colour");
		Check(Near(d.scale, 1.0f), "default params keep unit scale");
		Check(Near(d.alpha, 1.0f), "default fill runs at full alpha");
		Check(Near(d.uOffset, 0.0f) && Near(d.vOffset, 0.0f), "zero scroll speed leaves no offset");
	}

	{
		const Efsh::EffectParams grad = ThreeKeyGradient();
		const Efsh::FillState at0 = Efsh::Evaluate(grad, 0.0f, 1.0f, 1.0f);
		Check(NearVec(at0.color, Vec3{ 1.0f, 0.0f, 0.0f }), "at phase zero the colour is the first key");
		Check(Near(at0.scale, 1.0f), "at phase zero the scale is the first key scale");

		const Efsh::FillState first = Efsh::Evaluate(grad, 0.5f, 1.0f, 1.0f);
		Check(NearVec(first.color, Vec3{ 0.5f, 0.5f, 0.0f }), "the first segment lerps key zero to key one");
		Check(Near(first.scale, 1.5f), "the first segment lerps scale one to two");

		const Efsh::FillState second = Efsh::Evaluate(grad, 1.5f, 1.0f, 1.0f);
		Check(NearVec(second.color, Vec3{ 0.0f, 0.5f, 0.5f }), "the second segment lerps key one to key two");
		Check(Near(second.scale, 3.0f), "the second segment lerps scale two to four");

		const Efsh::FillState wrapped = Efsh::Evaluate(grad, 2.5f, 1.0f, 1.0f);
		Check(NearVec(wrapped.color, Vec3{ 0.5f, 0.5f, 0.0f }), "elapsed past the last key wraps by the period");
	}

	{
		Efsh::EffectParams pulse;
		pulse.fill.fullAlphaRatio = 1.0f;
		pulse.fill.persistentAlphaRatio = 0.5f;
		pulse.fill.pulseAmplitude = 1.0f;
		pulse.fill.pulseFrequency = 1.0f;
		Check(Near(Efsh::Evaluate(pulse, 0.0f, 1.0f, 1.0f).alpha, 0.5f), "the pulse starts at the persistent floor");
		Check(Near(Efsh::Evaluate(pulse, 0.25f, 1.0f, 1.0f).alpha, 0.75f), "the pulse crosses the midpoint quarter-cycle in");
		Check(Near(Efsh::Evaluate(pulse, 0.5f, 1.0f, 1.0f).alpha, 1.0f), "the pulse peaks at the full ratio half-cycle in");
	}

	{
		Efsh::EffectParams fade;
		fade.fill.fadeInTime = 2.0f;
		Check(Near(Efsh::Evaluate(fade, 0.5f, 1.0f, 1.0f).alpha, 0.25f), "the fade ramps alpha in during the fade window");
		Check(Near(Efsh::Evaluate(fade, 1.0f, 1.0f, 1.0f).alpha, 0.5f), "the fade is half done at half the window");
		Check(Near(Efsh::Evaluate(fade, 2.0f, 1.0f, 1.0f).alpha, 1.0f), "at the end of the window the fade is complete");
	}

	{
		Efsh::EffectParams full;
		Check(Near(Efsh::Evaluate(full, 5.0f, 1.0f, 0.5f).alpha, 0.5f), "intensity scales the fill alpha");
		Check(Near(Efsh::Evaluate(full, 5.0f, 1.0f, 3.0f).alpha, 1.0f), "an over-unit intensity clamps the alpha at one");
	}

	{
		Efsh::EffectParams scroll;
		scroll.animationSpeedU = 0.3f;
		scroll.animationSpeedV = 0.7f;
		const Efsh::FillState byElapsed = Efsh::Evaluate(scroll, 2.0f, 1.0f, 1.0f);
		Check(Near(byElapsed.uOffset, 0.6f), "the u scroll offset advances with elapsed time");
		Check(Near(byElapsed.vOffset, 0.4f), "the v scroll offset wraps within the unit period");

		const Efsh::FillState bySpeed = Efsh::Evaluate(scroll, 1.0f, 2.0f, 1.0f);
		Check(Near(bySpeed.uOffset, 0.6f) && Near(bySpeed.vOffset, 0.4f), "speed multiplies elapsed time into the same offsets");
	}

	{
		Efsh::EffectParams tint;
		tint.colorScale = 2.0f;
		Check(Near(Efsh::Evaluate(tint, 0.0f, 1.0f, 1.0f).scale, 2.0f), "the colour scale multiplies the key scale");

		Efsh::EffectParams negative;
		negative.colorScale = -1.0f;
		Check(Near(Efsh::Evaluate(negative, 0.0f, 1.0f, 1.0f).scale, 0.0f), "a negative colour scale floors the output scale at zero");
	}

	{
		Efsh::EffectParams edge;
		edge.edgeColor = Vec3{ 0.1f, 0.2f, 0.3f };
		edge.edge.fullAlphaRatio = 0.8f;
		edge.edge.persistentAlphaRatio = 0.8f;
		const Efsh::FillState out = Efsh::Evaluate(edge, 1.0f, 1.0f, 1.0f);
		Check(NearVec(out.edgeColor, Vec3{ 0.1f, 0.2f, 0.3f }), "the edge colour passes through unchanged");
		Check(Near(out.edgeAlpha, 0.8f), "the edge alpha runs its own params");
	}

	return test::Finish("efsh");
}
