// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Renumbering.h"
#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Layer Tinted(float a_opacity) { return Layer{.opacity = a_opacity}; }

SurfaceOutput Emissive(std::vector<Layer> a_stack) {
  return SurfaceOutput{.surface = Surface::kMaterial,
                       .slot = Slot::kEmissive,
                       .stack = std::move(a_stack)};
}

SurfaceOutput Diffuse(std::vector<Layer> a_stack) {
  return SurfaceOutput{.surface = Surface::kMaterial,
                       .slot = Slot::kDiffuse,
                       .stack = std::move(a_stack)};
}

Recipe Document() {
  Recipe recipe;
  recipe.id = "glow";
  recipe.outputs.emplace_back(
      Emissive({Tinted(0.1f), Tinted(0.2f), Tinted(0.3f)}));
  recipe.outputs.emplace_back(Diffuse({Tinted(0.5f)}));
  return recipe;
}

std::vector<Layer> &StackOf(Recipe &a_recipe, std::size_t a_output) {
  return std::get<SurfaceOutput>(a_recipe.outputs[a_output]).stack;
}

void UnchangedIndicesRenumberNothing() {
  const Recipe before = Document();
  Check(!RenumberingOf(before, before), "an unchanged recipe moves nothing");
  Recipe tuned = before;
  StackOf(tuned, 0)[1].opacity = 0.9f;
  Check(!RenumberingOf(before, tuned), "a value edit moves nothing");
  Recipe appended = before;
  StackOf(appended, 0).push_back(Tinted(0.7f));
  appended.outputs.emplace_back(LightOutput{});
  Check(!RenumberingOf(before, appended),
        "appended layers and outputs move nothing that existed");
}

void LayerEditsRenumberTheirOutput() {
  const Recipe before = Document();
  Recipe removed = before;
  StackOf(removed, 0).erase(StackOf(removed, 0).begin());
  const std::optional<Renumbering> removal = RenumberingOf(before, removed);
  Check(removal && !LayerAfter(*removal, {0, 0}) &&
            LayerAfter(*removal, {0, 2}) == LayerPlace{0, 1} &&
            LayerAfter(*removal, {1, 0}) == LayerPlace{1, 0},
        "a removed layer drops and later layers move down");

  const std::optional<Renumbering> insertion = RenumberingOf(removed, before);
  Check(insertion && LayerAfter(*insertion, {0, 0}) == LayerPlace{0, 1} &&
            LayerAfter(*insertion, {0, 1}) == LayerPlace{0, 2},
        "an undone removal moves later layers back up");

  Recipe moved = before;
  std::vector<Layer> &stack = StackOf(moved, 0);
  std::ranges::rotate(stack, stack.begin() + 1);
  const std::optional<Renumbering> move = RenumberingOf(before, moved);
  Check(move && LayerAfter(*move, {0, 0}) == LayerPlace{0, 2} &&
            LayerAfter(*move, {0, 1}) == LayerPlace{0, 0} &&
            LayerAfter(*move, {0, 2}) == LayerPlace{0, 1},
        "a moved layer keeps its identity at its new place");

  Recipe cascaded = before;
  StackOf(cascaded, 0) = {Tinted(0.2f)};
  const std::optional<Renumbering> cascade = RenumberingOf(before, cascaded);
  Check(cascade && !LayerAfter(*cascade, {0, 0}) &&
            LayerAfter(*cascade, {0, 1}) == LayerPlace{0, 0} &&
            !LayerAfter(*cascade, {0, 2}),
        "a cascade removal drops every layer it removed");

  Recipe replaced = before;
  StackOf(replaced, 0).erase(StackOf(replaced, 0).begin());
  StackOf(replaced, 0)[0].opacity = 0.8f;
  const std::optional<Renumbering> both = RenumberingOf(before, replaced);
  Check(both && !LayerAfter(*both, {0, 0}) && !LayerAfter(*both, {0, 1}) &&
            LayerAfter(*both, {0, 2}) == LayerPlace{0, 1},
        "a gap that cannot be told apart drops its layers");
}

void OutputEditsRenumberOutputs() {
  const Recipe before = Document();
  Recipe removed = before;
  removed.outputs.erase(removed.outputs.begin());
  const std::optional<Renumbering> removal = RenumberingOf(before, removed);
  Check(removal && !OutputAfter(*removal, 0) && OutputAfter(*removal, 1) == 0 &&
            !LayerAfter(*removal, {0, 1}) &&
            LayerAfter(*removal, {1, 0}) == LayerPlace{0, 0},
        "a removed output drops with its layers and later outputs move");

  Recipe edited = removed;
  StackOf(edited, 0).push_back(Tinted(0.6f));
  const std::optional<Renumbering> undo = RenumberingOf(edited, before);
  Check(undo && OutputAfter(*undo, 0) == 1 &&
            LayerAfter(*undo, {0, 0}) == LayerPlace{1, 0},
        "an output is matched by its slot when its layers changed too");

  Recipe cleared = before;
  cleared.outputs.clear();
  const std::optional<Renumbering> clear = RenumberingOf(before, cleared);
  Check(clear && !OutputAfter(*clear, 0) && !OutputAfter(*clear, 1),
        "clearing the outputs drops every output");
}

void UnusualInputStaysBounded() {
  Recipe twins;
  twins.outputs.emplace_back(Emissive({Tinted(1.0f), Tinted(1.0f)}));
  Recipe one = twins;
  StackOf(one, 0).pop_back();
  const std::optional<Renumbering> twin = RenumberingOf(twins, one);
  Check(twin && twin->outputs.size() == 1 &&
            twin->outputs[0].layers.size() == 2 &&
            LayerAfter(*twin, {0, 0}) == LayerPlace{0, 0} &&
            !LayerAfter(*twin, {0, 1}),
        "identical layers align in order");

  Recipe huge;
  huge.outputs.emplace_back(
      Emissive(std::vector<Layer>(kMaxRenumberedEntries + 1, Tinted(0.5f))));
  const std::optional<Renumbering> bounded = RenumberingOf(huge, twins);
  Check(bounded && !LayerAfter(*bounded, {0, 0}),
        "a stack beyond the bound drops its layers instead of aligning them");

  const Renumbering empty;
  Check(!OutputAfter(empty, 4) && !LayerAfter(empty, {4, 2}),
        "an index outside the renumbering resolves to nothing");
}

void ViewFollowsRenumbering() {
  const Recipe before = Document();
  Recipe removed = before;
  StackOf(removed, 0).erase(StackOf(removed, 0).begin());
  const Renumbering removal = *RenumberingOf(before, removed);

  View view;
  view.muted = {LayerKey{"glow", 0, 0}, LayerKey{"glow", 0, 2},
                LayerKey{"other", 0, 2}};
  view.isolation = Isolation{}.SoloLayer("glow", 0, 2, true);
  const View next = RenumberedView(view, "glow", removal);
  Check(next.muted ==
            std::set<LayerKey>{LayerKey{"glow", 0, 1}, LayerKey{"other", 0, 2}},
        "mutes move with their layers, drop with a removed layer, and "
        "leave other recipes alone");
  Check(next.isolation.TargetsLayer("glow", 0, 1),
        "a layer solo moves with its layer");

  view.isolation = Isolation{}.SoloLayer("glow", 0, 0, true);
  Check(!RenumberedView(view, "glow", removal).Isolating(),
        "the solo of a removed layer ends with the output it introduced");

  view.isolation =
      Isolation::ForOutput("glow", 0).SoloLayer("glow", 0, 0, true);
  const Isolation kept = RenumberedView(view, "glow", removal).isolation;
  Check(kept.TargetsOutput("glow", 0) && !kept.layer,
        "the solo of a removed layer keeps an output isolated before it");

  Recipe outputGone = before;
  outputGone.outputs.erase(outputGone.outputs.begin());
  view.isolation = Isolation{}.SoloOutput("glow", 1, true);
  Check(RenumberedView(view, "glow", *RenumberingOf(before, outputGone))
            .isolation.TargetsOutput("glow", 0),
        "an output solo moves with its output");
  view.isolation = Isolation{}.SoloOutput("glow", 0, true);
  Check(!RenumberedView(view, "glow", *RenumberingOf(before, outputGone))
             .Isolating(),
        "the solo of a removed output ends");
}
}

int main() {
  UnchangedIndicesRenumberNothing();
  LayerEditsRenumberTheirOutput();
  OutputEditsRenumberOutputs();
  UnusualInputStaysBounded();
  ViewFollowsRenumbering();
  return test::Finish("studio_renumbering");
}
