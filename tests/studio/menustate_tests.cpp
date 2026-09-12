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

  Reduce(state, ScratchRebuilt{});
  Check(!state.mask.dirty, "a rebuilt scratch preview is clean");
  Reduce(state, ClearMask{});
  Check(state.mask.terms.empty(), "ClearMask empties the terms");
  Check(state.mask.dirty, "ClearMask schedules a cleared scratch preview");

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

void PaintCommitWaitsForAcknowledgment() {
  MenuState state;
  Reduce(state, SetMode{Mode::kPaint});
  Reduce(state, BeginPaint{"glow", RecipeKey{}, Surface::kMaterial});
  AcknowledgePaintUpdate(state, PaintUpdateResult{0, 0, {}});
  Reduce(state, AddTerm{Term{}});
  Reduce(state, KeepPaint{PaintCommitRequest{1, "glow", "engraving", "1"}});
  Check(state.paint && state.paint->pendingCommit == 1 &&
            !state.mask.terms.empty(),
        "requesting a commit retains the paint session and its mask");
  AcknowledgePaintCommit(state, PaintCommitResult{2, std::nullopt});
  Check(state.paint && state.paint->pendingCommit == 1,
        "an unrelated acknowledgment cannot close the pending session");
  AcknowledgePaintCommit(
      state,
      PaintCommitResult{1, MakeDiagnostic(Severity::kError, "mask engraving",
                                          "name already used")});
  Check(state.paint && !state.paint->pendingCommit && state.paint->problem &&
            !state.mask.terms.empty() && state.maskHistory.UndoDepth() > 0,
        "a refused commit retains the editable mask and history and exposes "
        "the error");
  Reduce(state, KeepPaint{PaintCommitRequest{3, "glow", "retry", "1"}});
  AcknowledgePaintCommit(state, PaintCommitResult{1, std::nullopt});
  Check(state.paint && state.paint->pendingCommit == 3,
        "an acknowledgment from a previous attempt cannot finish a retry");
  AcknowledgePaintCommit(state, PaintCommitResult{3, std::nullopt});
  Check(!state.paint && state.mask.terms.empty() &&
            state.selection.recipeID == "glow",
        "a successful acknowledgment closes the session and selects the target "
        "recipe");

  Reduce(state, SetMode{Mode::kPaint});
  Reduce(state, BeginPaint{"next", RecipeKey{}, Surface::kShell});
  AcknowledgePaintCommit(state, PaintCommitResult{3, std::nullopt});
  Check(state.paint && state.paint->recipeID == "next",
        "a repeated snapshot result cannot close a new paint session");
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
  PaintCommitWaitsForAcknowledgment();
  return test::Finish("studio_menustate");
}
