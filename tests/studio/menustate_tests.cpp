#include "studio/Intent.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  MenuState state;
  Intent intent = SetMode{Mode::kPaint};
  Intents pending;
  pending.push_back(intent);
  Check(state.mode == Mode::kCompose && pending.size() == 1,
        "TODO: Reduce has one arm per Intent alternative; Post collects");
  return test::Finish("studio_menustate");
}
