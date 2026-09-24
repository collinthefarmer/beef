// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/ResourcePanels.h"

#include "menu/MenuWidgets.h"

#include "recipe/Recipe.h"
#include "studio/Create.h"
#include "studio/Intent.h"
#include "studio/Selection.h"

#include <algorithm>

#include <optional>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
[[nodiscard]] std::optional<Studio::Creation>
CreationOf(Studio::ResourceTab a_tab) {
  switch (a_tab) {
  case Studio::ResourceTab::kSignals:
    return Studio::NewSignal{};
  case Studio::ResourceTab::kCurves:
    return Studio::NewCurve{};
  case Studio::ResourceTab::kSources:
    return Studio::NewSource{};
  case Studio::ResourceTab::kMasks:
    return Studio::NewMask{};
  }
  return std::nullopt;
}
}

void PostResourceAdd(const Frame &a_frame, Studio::ResourceTab a_tab) {
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents) {
    return;
  }
  const std::optional<Studio::Creation> request = CreationOf(a_tab);
  if (!request) {
    return;
  }
  Studio::Created made = Studio::Create(*request, *a_frame.recipe);
  Studio::Post(*a_frame.intents,
               Studio::EditRecipe{a_frame.recipe->id, std::move(made.edits)});
  a_frame.state->pendingSelection = made.subject;
}

void DrawResourceRename(
    const Frame &a_frame, const std::string &a_name,
    const std::function<Studio::RecipeEdit(std::string)> &a_edit) {
  const bool blocked = a_frame.state->paint.has_value() ||
                       a_frame.state->pendingEditorChange.has_value() ||
                       a_frame.state->pendingIndexedEdit.has_value() ||
                       a_frame.state->pendingRecipeFile.has_value();
  Disabled(blocked, [&] {
    if (ImGui::Button("Rename")) {
      ImGui::OpenPopup("rename-resource");
    }
  });
  Tooltip(a_frame.state->paint
              ? "Finish the current mask draft first."
              : "Rename this resource and update its references.");
  if (!ImGui::BeginPopup("rename-resource")) {
    return;
  }
  const std::string name{LiveTextField("resource-name", a_name.c_str(),
                                       Studio::Width::Px(240.0f),
                                       a_frame.scale)};
  const auto taken = Studio::ReservedNames(Studio::NamesOf(*a_frame.recipe));
  const bool valid = IsName(name);
  const bool available = !std::ranges::contains(taken, name);
  if (!name.empty() && !valid) {
    Problem("Use letters, digits and underscores; do not start with a digit.");
  } else if (name != a_name && !available) {
    Problem("That name is already in use.");
  }
  Disabled(blocked || !valid || !available || name == a_name, [&] {
    if (ImGui::Button("Rename##confirm")) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id, a_edit(name));
      ImGui::CloseCurrentPopup();
    }
  });
  ImGui::EndPopup();
}
}
