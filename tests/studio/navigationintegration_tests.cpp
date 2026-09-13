#include "studio/Intent.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
MenuState EditingLayer() {
  MenuState state;
  state.selection.recipeID = "glow";
  state.selection.piece = PieceRef{1, 2, false};
  state.selection.subject = LayerSubject{0, 1};
  state.selection.layer = 1;
  state.navigation.back.push_back({state.selection, 25.0f});
  return state;
}
}

int main() {
  {
    MenuState state = EditingLayer();
    const Intent edit = EditRecipe{"glow", {MoveLayer{0, 1, 0}}};
    Check(AcceptIntent(state, edit),
          "a settled editor accepts an initial structural command");
    Reduce(state, edit);
    state.pendingIndexedEdit = PendingIndexedEdit{7, "glow"};
    Check(Is<RecipeSubject>(state.selection.subject) &&
              !state.selection.layer && state.navigation.back.empty(),
          "initial reduction invalidates positional subjects before installing "
          "the pending gate");
    Check(
        !AcceptIntent(state, edit) &&
            !AcceptIntent(state, EditRecipe{"other", {SetClockSpeed{2.0f}}}) &&
            !AcceptIntent(state, Undo{"glow"}) &&
            !AcceptIntent(state, Redo{"glow"}) &&
            !AcceptIntent(state, RenameRecipe{"glow", "new"}) &&
            !AcceptIntent(state, CreateRecipe{}),
        "pending structure blocks later authoring commands from overtaking its "
        "acknowledgment");
    Reduce(state, CreateRecipe{"unexpected", {}, {}});
    Check(state.selection.recipeID == "glow",
          "direct reduction cannot bypass a pending authoring gate");
    Check(
        !AcceptIntent(state, PickLayer{0}) &&
            !AcceptIntent(state, PickCell{}) &&
            !AcceptIntent(state, SoloLayer{}) &&
            !AcceptIntent(state, SoloOutput{}) &&
            !AcceptIntent(state, MuteLayer{}),
        "stale positions cannot select or isolate a different output or layer");
    Check(AcceptIntent(state, PickRecipe{"other"}) &&
              AcceptIntent(state, PickPiece{}) &&
              AcceptIntent(state, ViewGeometry{"body"}) &&
              AcceptIntent(state, SetFreeze{false, 0.0f}),
          "context navigation and clock controls remain available while "
          "settling");
    Reduce(state, PickRecipe{"other"});
    Check(state.pendingIndexedEdit &&
              state.pendingIndexedEdit->recipeID == "glow",
          "changing document does not lose the pending operation owner");
  }
  {
    MenuState state = EditingLayer();
    Reduce(state, EditRecipe{"other", {RemoveLayer{0, 0}}});
    Check(state.selection.layer == 1 &&
              Is<LayerSubject>(state.selection.subject),
          "an edit to another recipe cannot shift the active layer context");
    Reduce(state, Undo{"glow"});
    Check(!state.selection.layer &&
              Is<RecipeSubject>(state.selection.subject) &&
              state.navigation.back.empty(),
          "undo invalidates positional inspector history");
    state = EditingLayer();
    Reduce(state, Redo{"glow"});
    Check(Is<RecipeSubject>(state.selection.subject) &&
              state.navigation.back.empty(),
          "redo invalidates positional inspector history");
    state = EditingLayer();
    Reduce(state, PickRecipe{"other"});
    Check(Is<RecipeSubject>(state.selection.subject) &&
              state.navigation.back.empty() &&
              state.selection.recipeID == "other",
          "document changes clear the previous inspector and Back path");
    state = EditingLayer();
    Reduce(state, PickPiece{PieceRef{9, 8, false}});
    Check(state.selection.piece.actorID == 9 &&
              state.selection.recipeID.empty() &&
              Is<RecipeSubject>(state.selection.subject) &&
              state.navigation.back.empty(),
          "wearer changes cannot inherit an old document inspector");
  }
  {
    MenuState state = EditingLayer();
    state.pendingIndexedEdit = PendingIndexedEdit{9, "glow"};
    state.pendingRecipeFile = PendingIndexedEdit{9, "glow"};
    Snapshot snapshot;
    snapshot.editResults.push_back({8, "glow", std::nullopt});
    snapshot.editResults.push_back({9, "other", std::nullopt});
    FileOperationResult file;
    file.requestID = 9;
    file.recipeID = "glow";
    file.action = FileAction::kRevert;
    file.state = FileOperationState::kPending;
    snapshot.fileOperations.push_back(file);
    AcknowledgeEditorOperations(state, snapshot);
    Check(
        state.pendingIndexedEdit && state.pendingRecipeFile,
        "old edit results and in-progress file operations preserve both gates");
    snapshot.editResults.push_back({9, "glow", "Edit refused"});
    AcknowledgeEditorOperations(state, snapshot);
    Check(!state.pendingIndexedEdit && state.pendingRecipeFile &&
              !AcceptIntent(state, Undo{"glow"}),
          "edit refusal releases only its own request domain");
    file.state = FileOperationState::kFailed;
    file.recipeID = "other";
    snapshot.fileOperations.push_back(file);
    AcknowledgeEditorOperations(state, snapshot);
    Check(state.pendingRecipeFile.has_value(),
          "another document's file result cannot acknowledge this request");
    file.recipeID = "glow";
    snapshot.fileOperations.push_back(file);
    AcknowledgeEditorOperations(state, snapshot);
    Check(!state.pendingRecipeFile && AcceptIntent(state, Undo{"glow"}),
          "terminal file failure releases authoring without claiming success");
    state.pendingRecipeFile = PendingIndexedEdit{10, "glow"};
    file.requestID = 10;
    file.state = FileOperationState::kSucceeded;
    snapshot.fileOperations.push_back(file);
    AcknowledgeEditorOperations(state, snapshot);
    Check(!state.pendingRecipeFile,
          "successful file publication also releases the pending gate");
  }
  {
    MenuState state = EditingLayer();
    state.mode = Mode::kPaint;
    state.paint = PaintSession{};
    state.paint->recipeID = "glow";
    state.paint->ready = true;
    state.paint->sessionID = 3;
    state.pendingIndexedEdit = PendingIndexedEdit{2, "glow"};
    Check(!AcceptIntent(state, AddTerm{}) &&
              !AcceptIntent(state, BeginPaint{}) &&
              !AcceptIntent(state, UpdatePaint{}) &&
              !AcceptIntent(state, KeepPaint{}),
          "pending document operations block new Paint mutations");
    Check(AcceptIntent(state, EndPaint{}),
          "restoration and session cleanup remain available");
    Reduce(state, EndPaint{});
    Check(!state.paint && state.mode == Mode::kCompose &&
              state.pendingIndexedEdit.has_value(),
          "ending Paint releases its draft without forgetting a pending "
          "document edit");
  }
  {
    MenuState state = EditingLayer();
    state.lastPaintReset = 3;
    state.pendingIndexedEdit = PendingIndexedEdit{11, "glow"};
    state.pendingRecipeFile = PendingIndexedEdit{12, "glow"};
    AcknowledgePaintUpdate(state, PaintUpdateResult{0, 3, std::nullopt, true});
    Check(
        Is<LayerSubject>(state.selection.subject) &&
            !state.navigation.back.empty(),
        "a repeated load reset leaves navigation established afterward intact");
    AcknowledgePaintUpdate(state, PaintUpdateResult{0, 4, std::nullopt, true});
    Check(Is<RecipeSubject>(state.selection.subject) &&
              !state.selection.layer && state.navigation.back.empty() &&
              state.lastPaintReset == 4,
          "a fresh load reset invalidates inspectors even without an active "
          "Paint session");
    Check(state.pendingIndexedEdit && state.pendingRecipeFile,
          "load reset does not invent acknowledgments for pending operations");
  }
  {
    MenuState state = EditingLayer();
    Snapshot snapshot;
    PieceRow piece;
    piece.ref = state.selection.piece;
    RecipeRow recipe;
    recipe.id = "replacement";
    piece.recipes.push_back(recipe);
    snapshot.pieces.push_back(piece);
    ResolveEditorSelection(state, snapshot);
    Check(
        state.selection.recipeID == "replacement" &&
            Is<RecipeSubject>(state.selection.subject) &&
            !state.selection.layer && state.navigation.back.empty(),
        "recipe fallback cannot inherit an inspector from the previous recipe");
    state = EditingLayer();
    snapshot.pieces.front().ref.actorID = 9;
    snapshot.pieces.front().recipes.front().id = "glow";
    ResolveEditorSelection(state, snapshot);
    Check(state.selection.piece.actorID == 9 &&
              Is<RecipeSubject>(state.selection.subject) &&
              !state.selection.layer && state.navigation.back.empty(),
          "wearer fallback clears inspector context even when recipe names "
          "match");
    state = EditingLayer();
    state.selection.subject = SignalSubject{"strength"};
    state.selection.geometry = "removed";
    snapshot.pieces.front().ref = state.selection.piece;
    GeometryRow geometry;
    geometry.name = "remaining";
    snapshot.pieces.front().recipes.front().geometries.push_back(geometry);
    SignalRow signal;
    signal.name = "strength";
    snapshot.pieces.front().recipes.front().signals.push_back(signal);
    ResolveEditorSelection(state, snapshot);
    Check(state.selection.geometry == "remaining" &&
              state.selection.subject ==
                  InspectorSubject{SignalSubject{"strength"}} &&
              state.navigation.back.empty(),
          "geometry fallback retains the named input but drops old geometry "
          "history");
    state.navigation.scroll = 55.0f;
    state.navigation.back.push_back({state.selection, 10.0f});
    ResolveEditorSelection(state, snapshot);
    Check(state.navigation.scroll == 55.0f && state.navigation.back.size() == 1,
          "unchanged snapshot context preserves inspector history and scroll");
  }
  return test::Finish("studio_navigationintegration");
}
