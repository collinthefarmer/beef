// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/View.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  View view;
  view.muted.insert(LayerKey{"glow", 0, 1});
  view.isolation = Isolation::ForLayer("glow", 0, 1);
  Check(view.LayerShown("glow", 0, 1) && !view.LayerShown("glow", 0, 0),
        "an isolated layer is shown even when muted, and hides its siblings");
  Check(!view.OutputShown("glow", 1) && !view.RecipeShown("other"),
        "layer isolation restricts both its output and recipe");

  view.isolation = view.isolation.SoloLayer("glow", 0, 1, false);
  Check(view.OutputShown("glow", 0) && !view.OutputShown("glow", 1) &&
            view.LayerShown("glow", 0, 0) && !view.LayerShown("glow", 0, 1),
        "ending layer solo keeps the output isolated and restores muting");
  view.isolation = view.isolation.SoloOutput("glow", 0, false);
  Check(view.RecipeShown("glow") && !view.RecipeShown("other") &&
            view.OutputShown("glow", 1),
        "ending output solo preserves an independently isolated recipe");

  view.isolation = {};
  view.isolation = view.isolation.SoloOutput("glow", 0, true);
  Check(view.isolation.bySolo && view.Isolating(),
        "output solo remembers when it introduced recipe isolation");
  view.isolation = view.isolation.SoloLayer("glow", 0, 2, true);
  view.isolation = view.isolation.SoloLayer("glow", 0, 2, false);
  Check(view.isolation.bySolo && view.isolation.output == 0 &&
            !view.isolation.layer,
        "ending nested layer solo retains output solo and its origin");
  view.isolation = view.isolation.SoloOutput("glow", 0, false);
  Check(!view.Isolating() && view.RecipeShown("other"),
        "ending the output solo that introduced isolation releases it");

  view.isolation = Isolation{}.SoloLayer("glow", 0, 2, true);
  view.isolation = view.isolation.SoloRecipe("glow", true);
  Check(!view.isolation.bySolo && !view.isolation.output &&
            !view.isolation.layer,
        "explicit recipe isolation clears narrower solo state");
  view.ForgetRecipe("glow");
  Check(view.isolation == Isolation{} && view.muted.empty(),
        "forgetting the recipe clears all of its isolation and mute state");
  Check(Isolation::ForLayer("", 0, 1) == Isolation{},
        "an empty recipe cannot acquire output or layer isolation");

  View queued;
  const std::vector<ViewCommand> commands{
      {Isolation::ForRecipe("glow"), true},
      {Isolation::ForOutput("glow", 0), true},
      {Isolation::ForOutput("glow", 1), true},
      {Isolation::ForOutput("glow", 0), false}};
  for (const ViewCommand &command : commands) {
    [[maybe_unused]] const bool changed = ApplyViewCommand(queued, command);
  }
  Check(queued.isolation.TargetsOutput("glow", 1) && !queued.isolation.bySolo,
        "queued Solo commands preserve live recipe isolation and ignore a "
        "stale off for another output");
  Check(ApplyViewCommand(queued, {Isolation::ForOutput("glow", 1), false}) &&
            queued.isolation == Isolation::ForRecipe("glow"),
        "unsolo after rapid commands restores the explicitly isolated recipe");
  Check(!ApplyViewCommand(queued, {Isolation::ForRecipe("glow"), true}),
        "an unchanged authoritative view does not request a replan");
  Check(ApplyViewCommand(queued, {Isolation::ForOutput("glow", 0), true}) &&
            ApplyViewCommand(queued, {Isolation::ForOutput("glow", 1), true}),
        "changing output Solo within one recipe requires a replan");

  View layerSolo;
  Check(
      ApplyViewCommand(layerSolo, {Isolation::ForLayer("glow", 0, 2), true}) &&
          layerSolo.LayerShown("glow", 0, 2),
      "layer Solo can introduce its own recipe and output isolation");
  Check(
      ApplyViewCommand(layerSolo, {Isolation::ForLayer("glow", 0, 2), false}) &&
          !layerSolo.Isolating(),
      "unsolo restores the full chain when layer Solo introduced isolation");
  layerSolo.isolation = Isolation::ForRecipe("glow");
  [[maybe_unused]] const bool layerOn =
      ApplyViewCommand(layerSolo, {Isolation::ForLayer("glow", 0, 2), true});
  [[maybe_unused]] const bool layerOff =
      ApplyViewCommand(layerSolo, {Isolation::ForLayer("glow", 0, 2), false});
  Check(layerSolo.isolation == Isolation::ForRecipe("glow"),
        "layer Solo preserves explicit recipe isolation without retaining an "
        "output it introduced");

  View shown;
  shown.isolation = Isolation::ForOutput("glow", 0);
  View muted = shown;
  muted.muted.insert(LayerKey{"glow", 0, 2});
  muted.isolation.layer = 1;
  muted.soloPiece = PieceRef{0x14, 0x800};
  Check(!shown.ChangesInstalls(muted),
        "muting, layer isolation and a solo piece leave the installs alone");
  View other = shown;
  other.isolation.output = 1;
  Check(shown.ChangesInstalls(other),
        "isolating another output changes what is installed");
  View pinned = shown;
  pinned.pin = Pin{PieceRef{0x14, 0x800}, "glow"};
  Check(shown.ChangesInstalls(pinned), "a pin changes what is installed");

  View live;
  Check(live.IsLive(), "a fresh view is live");
  View frozen = live;
  frozen.freeze = true;
  View slowed = live;
  slowed.speed = 0.5f;
  View soloed = live;
  soloed.soloPiece = PieceRef{1, 2};
  View muting = live;
  muting.muted.insert(LayerKey{"glow", 0, 0});
  View isolating = live;
  isolating.isolation = Isolation::ForRecipe("glow");
  Check(!frozen.IsLive() && !slowed.IsLive() && !soloed.IsLive() &&
            !muting.IsLive() && !isolating.IsLive(),
        "freeze, speed, a piece solo, a mute and isolation are previews");

  return test::Finish("studio_isolation");
}
