#include "studio/PaintSession.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  PaintSession session;
  Check(session.surface == Surface::kMaterial && kScratchMask == "scratch",
        "TODO: paint session lifecycle, scratch mask, keep-and-discard edits");
  return test::Finish("studio_paintsession");
}
