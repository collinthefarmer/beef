#include "studio/Page.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  const Page empty;
  Check(ActorOf(empty) == 0 && BonesOf(empty).empty() &&
            test::Near(ScaleOf(empty), 1.0f),
        "an unresolved page yields zero actor, no bones, unit scale");

  PieceRow piece;
  piece.ref.actorID = 0x14;
  GeometryRow geometry;
  geometry.bones.push_back(BoneCoverage{"NPC Spine2 [Spn2]", 0.6f});
  geometry.bones.push_back(BoneCoverage{"NPC L Hand [LHnd]", 0.4f});
  Names names;
  Intents intents;

  Page page;
  page.piece = &piece;
  page.geometry = &geometry;
  page.names = &names;
  page.intents = &intents;
  page.scale = 1.25f;

  Check(ActorOf(page) == 0x14, "ActorOf reads the piece's actor id");
  Check(BonesOf(page).size() == 2 &&
            BonesOf(page)[0].name == "NPC Spine2 [Spn2]",
        "BonesOf views the selected geometry's bones without copying");
  Check(test::Near(ScaleOf(page), 1.25f),
        "ScaleOf returns the renderer-supplied ui scale");

  Check(page.intents != nullptr,
        "the intents sink is reachable through a const page reference");
  return test::Finish("studio_page");
}
