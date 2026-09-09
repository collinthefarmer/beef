#include "recipe/Importer.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main()
{
	EffectShaderRecord record;
	record.editorId = "EnchArmorMagickaFXS";
	record.fillTexture = "Effects\\DarkSwirls.dds";
	record.params.fill.fullAlphaRatio = 0.05f;

	Check(record.tileU == 1.0f && record.tileV == 1.0f, "an effect-shader record defaults its tiling");
	Check(record.editorId == "EnchArmorMagickaFXS", "the record keeps its editor id");
	Check(record.params.fill.fullAlphaRatio == 0.05f, "the record carries effect params");

	return test::Finish("importer");
}
