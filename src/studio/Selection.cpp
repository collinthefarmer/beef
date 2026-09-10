#include "studio/Selection.h"

#include "recipe/Recipe.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool IsMaterialOutput(const OutputRow &a_output) noexcept {
  return a_output.target != Target::kLight;
}

[[nodiscard]] bool WritesCell(const OutputRow &a_output, Surface a_surface,
                              Slot a_slot) noexcept {
  return IsMaterialOutput(a_output) && a_output.surface == a_surface &&
         a_output.slot == a_slot;
}

[[nodiscard]] const GeometryRow *
FindGeometry(const RecipeRow &a_recipe, std::string_view a_name) noexcept {
  const auto it =
      std::ranges::find(a_recipe.geometries, a_name, &GeometryRow::name);
  return it == a_recipe.geometries.end() ? nullptr : &*it;
}

void InjectPinnedRecipe(std::vector<ResolvedRecipe> &a_resolved,
                        const WornPiece &a_piece, const View &a_view,
                        std::span<const Recipe> a_loaded) {
  const std::string &id = a_view.pin->recipeID;
  const auto pinned = std::ranges::find(a_loaded, id, &Recipe::id);
  const bool present =
      std::ranges::any_of(a_resolved, [&](const ResolvedRecipe &a_r) {
        return a_r.recipe && a_r.recipe->id == id;
      });
  const std::vector<PieceKey> choices = KeyChoicesOf(a_piece);
  const PieceKey *choice = DefaultKeyChoice(choices);
  if (pinned != a_loaded.end() && !present && choice) {
    const RecipeKey key = RecipeKeyOf(*choice, choice->form.ToString());
    a_resolved.push_back(
        {&*pinned, key, pinned->priority.value_or(DefaultPriority(key.kind))});
  }
}

void FilterIsolated(std::vector<ResolvedRecipe> &a_resolved,
                    const View &a_view) {
  std::erase_if(a_resolved, [&](const ResolvedRecipe &a_r) {
    return !a_r.recipe || a_r.recipe->id != a_view.isolateRecipe;
  });
}
}

const PieceRow *SelectedPiece(const Snapshot &a_snapshot,
                              const Selection &a_selection) noexcept {
  for (const auto &piece : a_snapshot.pieces) {
    if (piece.ref == a_selection.piece) {
      return &piece;
    }
  }
  return a_snapshot.pieces.empty() ? nullptr : &a_snapshot.pieces.front();
}

const RecipeRow *SelectedRecipe(const PieceRow *a_piece,
                                const Selection &a_selection) noexcept {
  if (!a_piece || a_piece->recipes.empty()) {
    return nullptr;
  }
  for (const auto &recipe : a_piece->recipes) {
    if (recipe.id == a_selection.recipeID) {
      return &recipe;
    }
  }
  return &a_piece->recipes.back();
}

const GeometryRow *SelectedGeometry(const RecipeRow *a_recipe,
                                    const Selection &a_selection) noexcept {
  if (!a_recipe || a_recipe->geometries.empty()) {
    return nullptr;
  }
  const GeometryRow *named = FindGeometry(*a_recipe, a_selection.geometry);
  return named ? named : &a_recipe->geometries.front();
}

const OutputRow *SelectedOutput(const GeometryRow *a_geometry,
                                const Selection &a_selection) noexcept {
  if (!a_geometry || a_selection.target == Target::kLight ||
      !a_selection.slot) {
    return nullptr;
  }
  const Surface surface = SurfaceOf(a_selection.target);
  for (const auto &output : a_geometry->outputs) {
    if (WritesCell(output, surface, *a_selection.slot)) {
      return &output;
    }
  }
  return nullptr;
}

std::optional<PieceRef> RequestOf(const Selection &a_selection) noexcept {
  if (a_selection.piece.actorID == 0) {
    return std::nullopt;
  }
  return a_selection.piece;
}

void ResolveSelection(Selection &a_selection, const Snapshot &a_snapshot) {
  const PieceRow *piece = SelectedPiece(a_snapshot, a_selection);
  if (!piece) {
    return;
  }
  a_selection.piece = piece->ref;
  const RecipeRow *recipe = SelectedRecipe(piece, a_selection);
  if (!recipe) {
    return;
  }
  a_selection.recipeID = recipe->id;
  const GeometryRow *geometry = SelectedGeometry(recipe, a_selection);
  if (!geometry) {
    return;
  }
  a_selection.geometry = geometry->name;
  if (!a_selection.layer || a_selection.target == Target::kLight ||
      !a_selection.slot) {
    return;
  }
  const Surface surface = SurfaceOf(a_selection.target);
  for (const auto &output : geometry->outputs) {
    if (output.target != Target::kLight && output.surface == surface &&
        output.slot == *a_selection.slot) {
      if (*a_selection.layer >= output.layers.size()) {
        a_selection.layer.reset();
      }
      return;
    }
  }
  a_selection.layer.reset();
}

std::vector<ResolvedRecipe>
ViewedRecipes(std::vector<ResolvedRecipe> a_resolved, const WornPiece &a_piece,
              PieceRef a_ref, const View &a_view,
              std::span<const Recipe> a_loaded) {
  if (a_view.pin && a_view.pin->piece == a_ref) {
    InjectPinnedRecipe(a_resolved, a_piece, a_view, a_loaded);
  }
  if (a_view.Isolating()) {
    FilterIsolated(a_resolved, a_view);
  }
  return a_resolved;
}

std::vector<std::string> View::RecipeIDs() const {
  std::vector<std::string> out;
  if (Isolating()) {
    out.push_back(isolateRecipe);
  }
  for (const auto &key : muted) {
    if (std::ranges::find(out, key.recipeID) == out.end()) {
      out.push_back(key.recipeID);
    }
  }
  if (pin && std::ranges::find(out, pin->recipeID) == out.end()) {
    out.push_back(pin->recipeID);
  }
  return out;
}

void View::RenameRecipe(std::string_view a_from, std::string_view a_to) {
  if (isolateRecipe == a_from) {
    isolateRecipe = std::string{a_to};
  }
  std::set<LayerKey> renamed;
  for (const auto &key : muted) {
    renamed.insert(
        LayerKey{key.recipeID == a_from ? std::string{a_to} : key.recipeID,
                 key.output, key.layer});
  }
  muted = std::move(renamed);
  if (pin && pin->recipeID == a_from) {
    pin->recipeID = std::string{a_to};
  }
}

void View::ForgetRecipe(std::string_view a_id) {
  if (isolateRecipe == a_id) {
    isolateRecipe.clear();
    isolateOutput = -1;
    isolateLayer = -1;
    isolatedBySolo = false;
  }
  std::erase_if(muted,
                [&](const LayerKey &a_key) { return a_key.recipeID == a_id; });
  if (pin && pin->recipeID == a_id) {
    pin.reset();
  }
}
}
