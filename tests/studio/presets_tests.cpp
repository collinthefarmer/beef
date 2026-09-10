#include "studio/Presets.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  MaskPresets presets;
  MaskPreset preset;
  presets.presets.push_back(preset);
  Check(presets.presets.size() == 1,
        "TODO: ParsePresets reads the preset file; names come from mesh, not a "
        "duplicate table");
  return test::Finish("studio_presets");
}
