#include "studio/MenuState.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  {
    MenuState state;
    state.selection.piece = PieceRef{1, 2, false};
    state.selection.recipeID = "target";
    state.selection.geometry = "origin";
    state.selection.subject = LayerSubject{0, 1};
    state.selection.layer = 1;
    state.navigation.scroll = 60.0f;
    Reduce(state, SetMode{Mode::kPaint});
    Reduce(
        state,
        BeginPaint{
            "target", {}, Surface::kMaterial, 8, 0, PaintAssignment{0, 1, 3}});
    AcknowledgePaintUpdate(state, PaintUpdateResult{8, 0, {}});
    Reduce(state, ViewGeometry{"preview"});
    ResolveEditorSelection(state, Snapshot{});
    Check(state.paint && state.paint->previewGeometry == "preview" &&
              state.paint->origin.geometry == "origin",
          "scratch geometry selection survives unmatched document resolution "
          "independently of its destination");
    Reduce(state, AddTerm{Term{TermOp::kSet, "1", "all", RawTerm{}}});
    const std::size_t history = state.maskHistory.UndoDepth();
    Check(MaskTaskActive(state), "an open mask inspector uses mask history");
    Reduce(state, SetMode{Mode::kCompose});
    Check(state.paint && state.paint->sessionID == 8 &&
              state.mask.terms.size() == 1 &&
              state.maskHistory.UndoDepth() == history,
          "leaving the mask inspector retains its draft, session, and history");
    Check(!MaskTaskActive(state) && AcceptIntent(state, Undo{"target"}),
          "a suspended draft leaves recipe undo available");
    Reduce(state, PickRecipe{"other", true});
    Reduce(state, PickPiece{PieceRef{9, 10, false}});
    Check(
        state.paint && state.mask.terms.size() == 1,
        "visiting another recipe and wearer cannot discard the suspended mask");
    Reduce(state, SetMode{Mode::kPaint});
    Check(state.selection.piece.actorID == 1 &&
              state.selection.recipeID == "target" &&
              state.selection.subject == InspectorSubject{LayerSubject{0, 1}} &&
              state.navigation.scroll == 60.0f,
          "Resume restores the destination wearer, recipe, layer, and scroll");
    RecipeRow changed;
    changed.id = "target";
    changed.documentRevision = 4;
    ObservePaintRecipe(state, &changed);
    Check(state.paint && !state.paint->assignment &&
              state.paint->assignmentInvalid && state.mask.terms.size() == 1 &&
              Is<RecipeSubject>(state.paint->origin.subject),
          "a changed destination disables positional assignment without losing "
          "the mask draft");
    Reduce(state, RenameRecipe{"target", "renamed"});
    Check(state.paint->recipeID == "renamed" &&
              state.paint->origin.recipeID == "renamed",
          "renaming the destination preserves the suspended task owner");
    Reduce(state,
           KeepPaint{PaintCommitRequest{10, "renamed", "spine", "1", 8}});
    Check(state.paint && state.paint->pendingCommit == 10,
          "Keep retains the draft until acknowledged");
    Check(!AcceptIntent(state, EndPaint{}),
          "Discard cannot imply cancellation after Keep was submitted");
    Check(!AcceptIntent(state, Undo{"renamed"}) &&
              !AcceptIntent(state, EditRecipe{"renamed", {RemoveOutput{0}}}) &&
              AcceptIntent(state, PickRecipe{"other", true}),
          "pending Keep blocks competing mutations while allowing navigation");
    AcknowledgePaintCommit(state, PaintCommitResult{10, {}});
    Check(!state.paint && state.selection.recipeID == "renamed" &&
              state.mode == Mode::kCompose && state.mask.terms.empty(),
          "acknowledged Keep returns to the surviving destination inspector");
  }
  {
    MenuState state;
    state.selection.recipeID = "target";
    Reduce(state, SetMode{Mode::kPaint});
    Reduce(
        state,
        BeginPaint{
            "target", {}, Surface::kMaterial, 8, 0, PaintAssignment{0, 1, 3}});
    AcknowledgePaintUpdate(state, PaintUpdateResult{8, 0, {}});
    Reduce(state, AddTerm{Term{TermOp::kSet, "1", "all", RawTerm{}}});
    Reduce(state, PickRecipe{"other", true});
    Reduce(state, EditRecipe{"other", {RemoveOutput{0}}});
    Check(state.paint && state.paint->assignment,
          "editing another document leaves the suspended destination intact");
    Reduce(state, EditRecipe{"target", {RemoveOutput{0}}});
    Check(state.paint && !state.paint->assignment &&
              state.paint->assignmentInvalid && state.mask.terms.size() == 1,
          "destination structure changes invalidate assignment before a new "
          "snapshot");
    state.paint->assignment = PaintAssignment{0, 1, 3};
    Reduce(state, Undo{"target"});
    Check(!state.paint->assignment && state.mask.terms.size() == 1 &&
              state.maskHistory.UndoDepth() == 1,
          "recipe undo invalidates the destination without undoing the "
          "suspended mask");
  }
  {
    Recipe target;
    target.id = "target";
    target.keys = {RecipeKey{}};
    SurfaceOutput output = DefaultOutput(Surface::kMaterial, Slot::kEmissive);
    output.stack = {DefaultLayer(), DefaultLayer()};
    target.outputs = {output};
    const Recipe paint = PaintRecipe(target, RecipeKey{}, Surface::kMaterial);
    PaintCommitRequest request{
        1, "target", "spine", "1", 8, {}, PaintAssignment{0, 1, 7}};
    const auto edits = PreparePaintCommit(&paint, &target, request, 7);
    Check(edits.has_value(),
          "mask creation and destination binding prepare as one batch");
    if (edits) {
      const auto prepared = PrepareEdits(target, *edits);
      Check(prepared && prepared->FindMask("spine"),
            "the atomic batch creates the authored mask");
      if (prepared) {
        const auto *surface = Get<SurfaceOutput>(prepared->outputs.front());
        Check(surface && surface->stack.size() == 2 &&
                  surface->stack[1].mask == Ref{"spine"} &&
                  !surface->stack[0].mask,
              "the same batch assigns only the selected layer");
      }
    }
    Check(
        !PreparePaintCommit(&paint, &target, request, 8),
        "a changed document revision refuses stale assignment before writing");
    request.assignment->layer = 9;
    Check(!PreparePaintCommit(&paint, &target, request, 7),
          "a missing destination layer rejects the whole mask commit");
    request.assignment.reset();
    request.replacingMask = "spine";
    Check(!PreparePaintCommit(&paint, &target, request),
          "a deleted shared mask cannot be silently recreated under its old "
          "name");
    request.maskName = "new_spine";
    Check(PreparePaintCommit(&paint, &target, request).has_value(),
          "a retained draft can be explicitly kept under a new name after "
          "deletion");
  }
  return test::Finish("suspended paint");
}
