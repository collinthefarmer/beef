#include "studio/Intent.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
void PostCollectsWithoutReducing() {
  MenuState state;
  Intents pending;
  Post(pending, SetMode{Mode::kPaint});
  Post(pending, PickRecipe{"glow"});
  Check(pending.size() == 2, "Post appends each intent");
  Check(state.mode == Mode::kCompose,
        "Post alone does not touch state; only Reduce does");
}

void PostEditWrapsAsEditRecipe() {
  Intents pending;
  Post(pending, "glow", AddLayer{0, DefaultLayer(), std::nullopt});
  Check(pending.size() == 1, "an edit posts one intent");
  const EditRecipe *edit = Get<EditRecipe>(pending.front());
  Check(edit != nullptr && edit->recipeID == "glow" && edit->edits.size() == 1,
        "Post(recipe, edit) builds one EditRecipe carrying the edit");
}

void ModeSwitchSetsLayoutAndResource() {
  MenuState state;
  Reduce(state, SetMode{Mode::kPaint});
  Check(state.mode == Mode::kPaint, "SetMode changes the mode");
  Check(state.layout.mode == Mode::kPaint && state.layout.maskEditor,
        "SetMode installs the paint layout");
  Check(state.resource == ResourceTab::kMasks,
        "entering paint selects the mask resource tab");
}

void SelectionIntentsResolve() {
  MenuState state;
  Reduce(state, PickRecipe{"aurora"});
  Check(state.selection.recipeID == "aurora", "PickRecipe sets the recipe");
  Reduce(state, PickSlot{Slot::kNormal});
  Check(state.selection.slot == Slot::kNormal, "PickSlot sets the slot");
  Check(!state.selection.layer, "PickSlot clears the layer");
  Reduce(state, PickLayer{3});
  Check(state.selection.layer == std::size_t{3}, "PickLayer sets the layer");
}

void MaskIntentsBuildAndUndo() {
  MenuState state;
  Reduce(state, AddTerm{Term{}});
  Check(state.mask.terms.size() == 1, "AddTerm appends a term");
  Check(state.mask.terms.front().op == TermOp::kSet,
        "the first term is forced to kSet");
  Check(state.mask.selected == std::size_t{0}, "AddTerm selects the new term");
  Check(state.mask.dirty, "AddTerm marks the mask dirty");

  Reduce(state, AddTerm{Term{}});
  Check(state.mask.terms.size() == 2, "AddTerm appends a second term");
  Check(state.mask.terms[1].op == TermOp::kAnd,
        "a following term defaults to kAnd, not kSet");

  Reduce(state, ClearMask{});
  Check(state.mask.terms.empty(), "ClearMask empties the terms");

  Reduce(state, UndoMask{});
  Check(state.mask.terms.size() == 2, "UndoMask restores the pre-clear mask");
}

void EditRecipeRemapsSelectedLayer() {
  MenuState state;
  Reduce(state, PickLayer{2});
  std::vector<RecipeEdit> edits;
  edits.push_back(RemoveLayer{0, 0});
  Reduce(state, EditRecipe{"aurora", std::move(edits)});
  Check(state.selection.layer == std::size_t{1},
        "removing an earlier layer shifts the selected index down");
}

void RenameRecipeFollowsSelection() {
  MenuState state;
  Reduce(state, PickRecipe{"old"});
  Reduce(state, RenameRecipe{"old", "new"});
  Check(state.selection.recipeID == "new",
        "RenameRecipe retargets the selection");
}
}

int main() {
  PostCollectsWithoutReducing();
  PostEditWrapsAsEditRecipe();
  ModeSwitchSetsLayoutAndResource();
  SelectionIntentsResolve();
  MaskIntentsBuildAndUndo();
  EditRecipeRemapsSelectedLayer();
  RenameRecipeFollowsSelection();
  return test::Finish("studio_menustate");
}
