#include "mesh/TextureSize.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	Check(TextureSize{ 1024 }.Pixels() == 1024, "a size in range is kept");
	Check(TextureSize{ 0 }.Pixels() == TextureSize::kMin, "a size below the floor clamps up");
	Check(TextureSize{ 99999 }.Pixels() == TextureSize::kMax, "a size above the ceiling clamps down");
	Check(TextureSize::kMin == 64 && TextureSize::kMax == 4096, "the bounds are 64..4096");
	Check(TextureSize{ 512 } == TextureSize{ 512 }, "equal sizes compare equal");

	return test::Finish("texturesize");
}
