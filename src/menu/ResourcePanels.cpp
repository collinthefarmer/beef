// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/ResourcePanels.h"

#include "menu/MenuWidgets.h"

#include "recipe/Recipe.h"
#include "studio/Create.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/NameEdit.h"
#include "studio/Names.h"
#include "studio/Selection.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

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
  a_frame.state->pendingSelection =
      Studio::PendingSubject{a_frame.recipe->id, made.subject};
}

void DrawResourceRename(
    const Frame &a_frame, const std::string &a_name,
    const std::function<Studio::RecipeEdit(std::string)> &a_edit) {
  const bool blocked = Studio::IsRenameBlocked(*a_frame.state);
  ImGui::PushID("rename");
  Disabled(blocked, [&] {
    if (ImGui::Button("Rename")) {
      ImGui::OpenPopup("rename-resource");
    }
  });
  Tooltip(a_frame.state->paint
              ? "Finish the current mask draft first."
              : "Rename this resource and update its references.");
  if (!ImGui::BeginPopup("rename-resource")) {
    ImGui::PopID();
    return;
  }
  const std::string name{LiveTextField("resource-name", a_name.c_str(),
                                       Studio::Width::Px(240.0f),
                                       a_frame.scale)};
  const auto taken = Studio::ReservedNames(Studio::NamesOf(*a_frame.recipe));
  const std::optional<std::string> problem =
      name.empty()
          ? std::nullopt
          : Studio::NameProblem(name, {.current = a_name, .taken = taken});
  if (problem) {
    Problem(*problem);
  }
  Disabled(blocked || name.empty() || problem || name == a_name, [&] {
    if (ImGui::Button("Rename##confirm")) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id, a_edit(name));
      ImGui::CloseCurrentPopup();
    }
  });
  ImGui::EndPopup();
  ImGui::PopID();
}

void DrawResourceClone(const Frame &a_frame,
                       const Studio::InspectorSubject &a_subject) {
  ImGui::PushID("clone");
  if (ImGui::Button("Clone")) {
    Studio::Created made =
        Studio::Create(Studio::NewCopy{a_subject}, *a_frame.recipe);
    if (!made.edits.empty()) {
      Studio::Post(*a_frame.intents, Studio::EditRecipe{a_frame.recipe->id,
                                                        std::move(made.edits)});
      a_frame.state->pendingSelection =
          Studio::PendingSubject{a_frame.recipe->id, made.subject};
    }
  }
  Tooltip("Copy this resource under a new name.");
  ImGui::PopID();
}

NameLabelSpec ResourceLabel(const Frame &a_frame, const char *a_key,
                            std::string_view a_name) {
  const Studio::RecipeRow *recipe = a_frame.recipe;
  return NameLabelSpec{.key = a_key,
                       .name = a_name,
                       .taken =
                           [recipe]() {
                             return recipe ? Studio::ReservedNames(
                                                 Studio::NamesOf(*recipe))
                                           : std::vector<std::string>{};
                           },
                       .editable = !Studio::IsRenameBlocked(*a_frame.state),
                       .size = {Studio::Width::Px(240.0f), a_frame.scale}};
}

void PostResourceRename(const Frame &a_frame,
                        const Studio::InspectorSubject &a_subject,
                        std::string a_to) {
  std::optional<Studio::RecipeEdit> edit =
      Studio::ResourceRename(a_subject, std::move(a_to));
  if (edit && a_frame.recipe && a_frame.intents) {
    Studio::Post(*a_frame.intents, a_frame.recipe->id, std::move(*edit));
  }
}
}
