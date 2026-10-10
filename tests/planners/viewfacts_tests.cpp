// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ViewFacts.h"
#include "test_support.h"

#include <optional>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
ViewInputs Culled(std::optional<bool> a_camera, bool a_inspected = false) {
  return {ActorRole::kOther, true, a_inspected, a_camera};
}

void ThePlayerIsAlwaysInView() {
  Check(InViewOf({ActorRole::kPlayer, true, false, false}),
        "the player is in view when the camera misses it");
  Check(InViewOf({ActorRole::kPlayer, true, false, std::nullopt}),
        "the player is in view without a camera reading");
}

void CullOffShowsEveryActor() {
  Check(InViewOf({ActorRole::kOther, false, false, false}),
        "with cull off an actor outside the camera is in view");
  Check(InViewOf({ActorRole::kOther, false, false, std::nullopt}),
        "with cull off an actor without a camera reading is in view");
}

void TheCameraDecidesWhenCulled() {
  Check(InViewOf(Culled(true)), "an actor in the camera is in view");
  Check(!InViewOf(Culled(false)), "an actor outside the camera is not");
}

void TheInspectedActorIsInView() {
  Check(InViewOf(Culled(false, true)),
        "the inspected actor outside the camera is in view");
  Check(InViewOf(Culled(std::nullopt, true)),
        "the inspected actor without a camera reading is in view");
}

void AnUnknownCameraIsOutOfView() {
  Check(!InViewOf(Culled(std::nullopt)),
        "an actor without a camera reading is out of view when culled");
}

void ScreenSizingOffReadsFull() {
  Check(ScreenFractionOf(0.25f, false, std::nullopt) == 1.0f,
        "screen sizing off reads the full fraction");
  Check(ScreenFractionOf(std::nullopt, false, std::nullopt) == 1.0f,
        "screen sizing off reads full for an unmeasured actor");
}

void ScreenSizingOnReadsTheRawFraction() {
  Check(ScreenFractionOf(0.25f, true, std::nullopt) == 0.25f,
        "screen sizing on reads the measured fraction");
  Check(!ScreenFractionOf(std::nullopt, true, std::nullopt),
        "screen sizing on leaves an unmeasured actor unmeasured");
}

void TheRegressionOverrideWins() {
  Check(ScreenFractionOf(0.25f, false, true) == 0.25f,
        "an override on measures with the setting off");
  Check(ScreenFractionOf(0.25f, true, false) == 1.0f,
        "an override off reads full with the setting on");
  Check(!ScreenFractionOf(std::nullopt, false, true),
        "an override on leaves an unmeasured actor unmeasured");
}
}

int main() {
  ThePlayerIsAlwaysInView();
  CullOffShowsEveryActor();
  TheCameraDecidesWhenCulled();
  TheInspectedActorIsInView();
  AnUnknownCameraIsOutOfView();
  ScreenSizingOffReadsFull();
  ScreenSizingOnReadsTheRawFraction();
  TheRegressionOverrideWins();
  return test::Finish("planners_viewfacts");
}
