#include "studio/View.h"

#include <utility>

namespace BetterEnchantmentEffects::Studio {
bool Isolation::TargetsOutput(std::string_view a_recipe,
                              std::size_t a_output) const noexcept {
  return recipeID == a_recipe && output == a_output;
}

bool Isolation::TargetsLayer(std::string_view a_recipe, std::size_t a_output,
                             std::size_t a_layer) const noexcept {
  return TargetsOutput(a_recipe, a_output) && layer == a_layer;
}

Isolation Isolation::ForRecipe(std::string a_recipe) {
  return Isolation{std::move(a_recipe), std::nullopt, std::nullopt, false};
}

Isolation Isolation::ForOutput(std::string a_recipe, std::size_t a_output) {
  Isolation isolation = ForRecipe(std::move(a_recipe));
  if (!isolation.recipeID.empty()) {
    isolation.output = a_output;
  }
  return isolation;
}

Isolation Isolation::ForLayer(std::string a_recipe, std::size_t a_output,
                              std::size_t a_layer) {
  Isolation isolation = ForOutput(std::move(a_recipe), a_output);
  if (isolation.output) {
    isolation.layer = a_layer;
  }
  return isolation;
}

Isolation Isolation::SoloRecipe(std::string a_recipe, bool a_on) const {
  if (!a_on && recipeID != a_recipe) {
    return *this;
  }
  return a_on ? ForRecipe(std::move(a_recipe)) : Isolation{};
}

Isolation Isolation::SoloOutput(std::string a_recipe, std::size_t a_output,
                                bool a_on) const {
  if (!a_on) {
    if (!TargetsOutput(a_recipe, a_output)) {
      return *this;
    }
    return bySolo ? Isolation{} : ForRecipe(recipeID);
  }
  Isolation isolation = ForOutput(std::move(a_recipe), a_output);
  isolation.bySolo = bySolo || recipeID.empty();
  return isolation;
}

Isolation Isolation::SoloLayer(std::string a_recipe, std::size_t a_output,
                               std::size_t a_layer, bool a_on) const {
  if (!a_on) {
    if (!TargetsLayer(a_recipe, a_output, a_layer)) {
      return *this;
    }
    if (outputBySolo) {
      return bySolo ? Isolation{} : ForRecipe(recipeID);
    }
    Isolation isolation = *this;
    isolation.layer.reset();
    return isolation;
  }
  Isolation isolation = ForLayer(std::move(a_recipe), a_output, a_layer);
  isolation.bySolo = bySolo || recipeID.empty();
  isolation.outputBySolo = outputBySolo || !output;
  return isolation;
}
bool ApplyViewCommand(View &a_view, const ViewCommand &a_command) {
  if (a_command.piece) {
    std::optional<PieceRef> next = a_view.soloPiece;
    if (a_command.on) {
      next = *a_command.piece;
    } else if (a_view.soloPiece &&
               a_view.soloPiece->SamePiece(*a_command.piece)) {
      next = std::nullopt;
    }
    if (next == a_view.soloPiece) {
      return false;
    }
    a_view.soloPiece = next;
    return true;
  }
  const Isolation &target = a_command.target;
  Isolation next;
  if (target.output && target.layer) {
    next = a_view.isolation.SoloLayer(target.recipeID, *target.output,
                                      *target.layer, a_command.on);
  } else if (target.output) {
    next = a_view.isolation.SoloOutput(target.recipeID, *target.output,
                                       a_command.on);
  } else {
    next = a_view.isolation.SoloRecipe(target.recipeID, a_command.on);
  }
  if (next == a_view.isolation) {
    return false;
  }
  a_view.isolation = std::move(next);
  return true;
}

}
