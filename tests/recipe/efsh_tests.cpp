#include "recipe/Efsh.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

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

	return test::Finish("efsh");
}
