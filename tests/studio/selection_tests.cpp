// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Selection.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Snapshot MakeSnapshot() {
  Snapshot snapshot;

  OutputRow emissive;
  emissive.index = 0;
  emissive.target = Target::kMaterial;
  emissive.surface = Surface::kMaterial;
  emissive.slot = Slot::kEmissive;
  emissive.layers.resize(2);

  GeometryRow geometry;
  geometry.name = "body";
  geometry.outputs.push_back(emissive);

  RecipeRow recipe;
  recipe.id = "glow";
  recipe.geometries.push_back(geometry);

  PieceRow piece;
  piece.ref = PieceRef{.actorID = 1, .armorID = 2, .firstPerson = false};
  piece.recipes.push_back(recipe);

  snapshot.pieces.push_back(piece);
  return snapshot;
}

Selection ValidSelection() {
  Selection selection;
  selection.piece = PieceRef{.actorID = 1, .armorID = 2, .firstPerson = false};
  selection.recipeID = "glow";
  selection.geometry = "body";
  selection.target = Target::kMaterial;
  selection.slot = Slot::kEmissive;
  selection.layer = 0;
  return selection;
}
}

int main() {
  const Snapshot snapshot = MakeSnapshot();

  {
    Selection selection = ValidSelection();
    const PieceRow *piece = SelectedPiece(snapshot, selection);
    Check(piece != nullptr && piece->ref.actorID == 1,
          "valid selection resolves the matching piece");
    const RecipeRow *recipe = SelectedRecipe(piece, selection);
    Check(recipe != nullptr && recipe->id == "glow",
          "valid selection resolves the matching recipe");
    const GeometryRow *geometry = SelectedGeometry(recipe, selection);
    Check(geometry != nullptr && geometry->name == "body",
          "valid selection resolves the matching geometry");
    const OutputRow *output = SelectedOutput(geometry, selection);
    Check(output != nullptr && output->slot == Slot::kEmissive,
          "valid selection resolves the emissive output");

    ResolveSelection(selection, snapshot);
    Check(selection.piece.actorID == 1 && selection.recipeID == "glow" &&
              selection.geometry == "body" && selection.layer == 0,
          "resolving a valid selection leaves it intact");
  }

  {
    Selection stale;
    stale.piece = PieceRef{.actorID = 999, .armorID = 0, .firstPerson = false};
    stale.recipeID = "missing";
    stale.geometry = "missing";
    stale.target = Target::kMaterial;
    stale.slot = Slot::kEmissive;
    stale.layer = 5;
    ResolveSelection(stale, snapshot);
    Check(stale.piece.actorID == 1 && stale.recipeID == "glow" &&
              stale.geometry == "body",
          "stale selection falls back to defined rows, never out of bounds");
    Check(!stale.layer.has_value(),
          "stale out-of-range layer is cleared, not indexed");
  }

  {
    Snapshot empty;
    Selection selection = ValidSelection();
    Check(SelectedPiece(empty, selection) == nullptr,
          "empty snapshot has no selected piece");
    ResolveSelection(selection, empty);
    Check(selection.recipeID == "glow",
          "resolving against an empty snapshot is inert");
    Check(SelectedRecipe(nullptr, selection) == nullptr &&
              SelectedGeometry(nullptr, selection) == nullptr &&
              SelectedOutput(nullptr, selection) == nullptr,
          "null parents resolve to null, never dereferenced");
  }

  {
    Selection none;
    Check(!RequestOf(none).has_value(),
          "a zero-actor selection requests no piece");
    Selection some = ValidSelection();
    const std::optional<PieceRef> request = RequestOf(some);
    Check(request.has_value() && request->actorID == 1,
          "a set selection requests its piece");
  }

  {
    View view;
    view.isolation.recipeID = "glow";
    view.isolation.output = 0;
    view.isolation.layer = 0;
    view.muted.insert(LayerKey{"glow", 0, 1});
    view.pin = Pin{PieceRef{.actorID = 1, .armorID = 2}, "glow"};

    std::vector<std::string> ids = view.RecipeIDs();
    Check(ids.size() == 1 && ids.front() == "glow",
          "recipe ids collapse the isolate, muted and pin references");

    view.RenameRecipe("glow", "spark");
    Check(view.isolation.recipeID == "spark" &&
              view.muted.contains(LayerKey{"spark", 0, 1}) && view.pin &&
              view.pin->recipeID == "spark",
          "renaming a recipe rewrites every reference in the view");

    view.ForgetRecipe("spark");
    Check(view.isolation.recipeID.empty() && !view.isolation.output &&
              !view.isolation.layer && view.muted.empty() &&
              !view.pin.has_value(),
          "forgetting a recipe drops every reference in the view");
  }

  {
    Recipe shadowed;
    shadowed.id = "shadowed";
    shadowed.keys = {
        RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"*"}}}};
    Recipe winner = shadowed;
    winner.id = "winner";
    Recipe unmatched = shadowed;
    unmatched.id = "unmatched";
    unmatched.keys = {RecipeKey{KeyKind::kMaterial,
                                KeyOperandValue{std::string{"other.dds"}}}};
    const std::vector<Recipe> loaded{shadowed, winner, unmatched};
    WornPiece piece;
    piece.diffusePaths = {"body.dds"};
    piece.armor = FormKey{"Test.esp", 0x123};
    const PieceRef ref{1, 2, false};
    View view;
    const auto normal = Resolve(piece, loaded);
    Check(normal.size() == 2 && normal.front().recipe == &loaded[0] &&
              normal.back().recipe == &loaded[1],
          "same-key identities normally resolve in definition order");
    view.isolation = Isolation::ForRecipe("shadowed");
    const auto isolated = ViewedRecipes({normal, piece, ref, view, loaded});
    Check(isolated.size() == 1 && isolated.front().recipe == &loaded[0],
          "recipe Solo selects its explicit recipe from independent same-key "
          "candidates");
    view.isolation = {};
    const auto restored = ViewedRecipes({normal, piece, ref, view, loaded});
    Check(restored.size() == 2 && restored.front().recipe == &loaded[0] &&
              restored.back().recipe == &loaded[1],
          "ending recipe Solo restores all normal same-key candidates");
    view.isolation = Isolation::ForRecipe("unmatched");
    Check(ViewedRecipes({normal, piece, ref, view, loaded}).empty(),
          "recipe Solo does not force an unmatched recipe onto the piece");
    view.pin = Pin{ref, "unmatched"};
    const auto pinned = ViewedRecipes({normal, piece, ref, view, loaded});
    Check(pinned.size() == 1 && pinned.front().recipe == &loaded[2],
          "an explicit pin still permits previewing the unmatched isolated "
          "recipe");
    view.pin->piece.actorID = 99;
    Check(ViewedRecipes({normal, piece, ref, view, loaded}).empty(),
          "a pin on another piece cannot force the isolated recipe to apply");
  }

  {
    Snapshot documents = MakeSnapshot();
    RecipeRow document;
    document.id = "unmatched";
    documents.documents.push_back(document);
    Selection selection = ValidSelection();
    selection.document = true;
    selection.recipeID = "unmatched";
    ResolveSelection(selection, documents);
    Check(selection.recipeID == "unmatched" && selection.geometry.empty() &&
              SelectedRecipe(documents, selection) ==
                  &documents.documents.front(),
          "an explicit unmatched document remains selected without fabricated "
          "geometry");
    Check(SelectedRecipe(&documents.pieces.front(), selection) == nullptr,
          "the live recipe resolver cannot substitute another match for a "
          "document");
    documents.documents.clear();
    ResolveSelection(selection, documents);
    Check(selection.recipeID == "unmatched" &&
              SelectedRecipe(documents, selection) == nullptr,
          "missing or not-yet-published documents preserve identity without "
          "falling back");
    documents.pieces.clear();
    documents.documents.push_back(document);
    ResolveSelection(selection, documents);
    Check(selection.recipeID == "unmatched" &&
              SelectedRecipe(documents, selection) ==
                  &documents.documents.front(),
          "document editing does not require a wearer or equipped armor");
  }
  {
    Snapshot documents = MakeSnapshot();
    RecipeRow document;
    document.id = "glow";
    documents.documents.push_back(document);
    Selection selection = ValidSelection();
    selection.document = true;
    Check(SelectedRecipe(documents, selection) ==
              &documents.pieces.front().recipes.front(),
          "a document applied to the selected piece retains its live "
          "observations");
    RecipeRow authored;
    authored.id = "glow";
    OutputRow first;
    first.index = 2;
    OutputRow second;
    second.index = 4;
    authored.outputs = {first, second};
    selection.subject = LayerSubject{4, 0};
    Check(SelectedAuthoredOutput(authored, selection) ==
              &authored.outputs.back(),
          "authored selection uses exact output identity when two outputs "
          "share a slot");
    selection.subject = OutputSubject{99};
    Check(
        !SelectedAuthoredOutput(authored, selection),
        "stale authored output indices never fall back to another slot match");
  }
  return test::Finish("studio_selection");
}
