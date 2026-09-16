#include "menu/ContextRows.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Selection.h"
#include "studio/SelectorEdit.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr Studio::TableStyle kContextStyle{.borders =
                                               Studio::TableBorders::kAll,
                                           .stretch = false,
                                           .headers = true,
                                           .rowBackground = false};
constexpr Studio::TableStyle kFormStyle{
    .borders = Studio::TableBorders::kInnerHorizontal,
    .stretch = true,
    .headers = false,
    .rowBackground = false};

[[nodiscard]] std::string RecipeLabel(const Studio::RecipeRow &a_recipe) {
  return a_recipe.pinned ? std::format("{} (pinned here)", a_recipe.id)
                         : a_recipe.id;
}

void SelectionCombo(const Studio::Snapshot &a_snapshot,
                    const Studio::PieceRow *a_piece, const char *a_label,
                    Studio::Intents &a_out) {
  const std::string preview =
      a_piece
          ? std::format("{} / {} ({})", a_piece->actorName, a_piece->armorName,
                        a_piece->ref.firstPerson ? "1st" : "3rd")
          : std::string{"nothing applied"};
  if (!ImGui::BeginCombo(a_label, preview.c_str())) {
    return;
  }
  std::size_t i = 0;
  for (const auto &piece : a_snapshot.pieces) {
    const std::string label =
        std::format("{} / {} ({})##sel{}", piece.actorName, piece.armorName,
                    piece.ref.firstPerson ? "1st" : "3rd", i++);
    if (ImGui::Selectable(label.c_str(), &piece == a_piece)) {
      Studio::Post(a_out, Studio::PickPiece{piece.ref});
    }
  }
  ImGui::EndCombo();
}

void RecipeCombo(const Frame &a_frame, const char *a_label) {
  const std::string preview = a_frame.recipe ? RecipeLabel(*a_frame.recipe)
                                             : SelectionOf(a_frame).recipeID;
  if (!ImGui::BeginCombo(a_label,
                         preview.empty() ? "Choose recipe" : preview.c_str())) {
    return;
  }
  for (const std::string &id : a_frame.snapshot->loaded) {
    const bool selected = id == SelectionOf(a_frame).recipeID;
    if (ImGui::Selectable(id.c_str(), selected)) {
      Studio::Post(*a_frame.intents, Studio::PickRecipe{id, true});
    }
  }
  ImGui::EndCombo();
}

void DrawKeysTable(const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  auto table = Table::Begin("keys",
                            {{"key", Studio::Width::Fit()},
                             {"", Studio::Width::Px(RowButtonWidth())}},
                            kFormStyle);
  if (!table.Open()) {
    return;
  }
  for (std::size_t i = 0; i < a_recipe.keys.size(); ++i) {
    const auto &key = a_recipe.keys[i];
    ImGui::PushID(static_cast<int>(i));
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(key.ToString().c_str());
    table.Cell();
    Disabled(a_recipe.keys.size() == 1, [&]() {
      if (RemoveButton(0)) {
        Studio::Post(a_out, a_recipe.id, Studio::RemoveKey{key});
      }
    });
    ImGui::PopID();
  }
  table.End();
}

void KeysPopup(const Studio::PieceRow &a_piece,
               const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  if (!ImGui::BeginPopup("recipe-keys")) {
    return;
  }
  DrawKeysTable(a_recipe, a_out);
  NextItemWidth(Studio::Width::Px(240.0f));
  if (ImGui::BeginCombo("##add-key", "add a key the piece carries")) {
    for (const auto &choice : a_piece.keys) {
      const RecipeKey key = RecipeKeyOf(choice.key, choice.text);
      if (std::ranges::find(a_recipe.keys, key) != a_recipe.keys.end()) {
        continue;
      }
      const std::string label =
          std::format("{}: {}", KeyKindName(choice.key.kind), choice.text);
      if (ImGui::Selectable(label.c_str(), false)) {
        Studio::Post(a_out, a_recipe.id, Studio::AddKey{key});
      }
    }
    ImGui::EndCombo();
  }
  const std::string_view keyword = LiveTextField(
      "keyword", "keyword editor id", Studio::Width::Px(240.0f), 1.0f);
  ImGui::SameLine();
  Disabled(keyword.empty(), [&]() {
    if (ImGui::SmallButton("Add keyword")) {
      RecipeKey key;
      key.kind = KeyKind::kKeyword;
      key.operand = FormRef::From(keyword);
      Studio::Post(a_out, a_recipe.id, Studio::AddKey{key});
    }
  });
  Tooltip("a keyword by editor id, resolved against the loaded plugins; the "
          "recipe then applies to every piece carrying it");
  ImGui::EndPopup();
}

void UndoRedoButtons(const Studio::RecipeRow &a_recipe,
                     Studio::Intents &a_out) {
  Disabled(a_recipe.undoDepth == 0, [&]() {
    if (ImGui::Button("Undo") && a_recipe.undoDepth > 0) {
      Studio::Post(a_out, Studio::Undo{a_recipe.id});
    }
  });
  Tooltip(std::format("{} edit(s) to undo (Ctrl+Z)", a_recipe.undoDepth));
  ImGui::SameLine();
  Disabled(a_recipe.redoDepth == 0, [&]() {
    if (ImGui::Button("Redo") && a_recipe.redoDepth > 0) {
      Studio::Post(a_out, Studio::Redo{a_recipe.id});
    }
  });
  Tooltip(std::format("{} edit(s) to redo (Ctrl+Y)", a_recipe.redoDepth));
}

void DrawRecipeContext(const Frame &a_frame) {
  auto table = Table::Begin(
      "recipe-context",
      {{"selection", Studio::Width::Fill()}, {"recipe", Studio::Width::Fill()}},
      kContextStyle);
  if (!table.Open()) {
    return;
  }
  table.Cell();
  NextItemWidth(Studio::Width::Fill());
  SelectionCombo(*a_frame.snapshot, a_frame.piece, "##selection",
                 *a_frame.intents);
  table.Cell();
  NextItemWidth(Studio::Width::Fill());
  RecipeCombo(a_frame, "##recipe");
  table.End();
}

}

std::optional<RecipeKey> DefaultKeyOf(const Studio::PieceRow &a_piece) {
  const Studio::KeyChoice *chosen = nullptr;
  for (const auto &key : a_piece.keys) {
    if (key.key.kind == KeyKind::kArmor) {
      chosen = &key;
      break;
    }
  }
  if (!chosen && !a_piece.keys.empty()) {
    chosen = &a_piece.keys.front();
  }
  if (!chosen) {
    return std::nullopt;
  }
  return RecipeKeyOf(chosen->key, chosen->text);
}

namespace {
void CreateForSelectedArmor(const Frame &a_frame, const std::string &a_name,
                            bool a_nameValid) {
  if (!a_frame.piece) {
    return;
  }
  const auto key = DefaultKeyOf(*a_frame.piece);
  if (!key) {
    return;
  }
  Disabled(!a_nameValid, [&] {
    if (ImGui::Button("Create for selected armor")) {
      Studio::Post(*a_frame.intents, Studio::CreateRecipe{a_name, *key, {}});
      ImGui::CloseCurrentPopup();
    }
  });
}

void NewRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("New recipe")) {
    ImGui::OpenPopup("new-recipe");
  }
  if (!ImGui::BeginPopup("new-recipe")) {
    return;
  }
  static KeyKind kind = KeyKind::kArmor;
  const std::string proposed =
      Studio::UniqueName("recipe", a_frame.snapshot->loaded);
  const std::string_view typed = LiveTextField(
      "new-name", proposed.c_str(), Studio::Width::Px(240.0f), a_frame.scale);
  const std::string name = typed.empty() ? proposed : std::string{typed};
  const KeyKind kinds[]{KeyKind::kArmor,        KeyKind::kKeyword,
                        KeyKind::kEnchantment,  KeyKind::kMagicEffect,
                        KeyKind::kEffectShader, KeyKind::kMaterial};
  if (ImGui::BeginCombo("Key type", std::string{KeyKindName(kind)}.c_str())) {
    for (const KeyKind choice : kinds) {
      if (ImGui::Selectable(std::string{KeyKindName(choice)}.c_str(),
                            choice == kind)) {
        kind = choice;
      }
    }
    ImGui::EndCombo();
  }
  const std::string_view operand = LiveTextField(
      "new-key",
      kind == KeyKind::kMaterial ? "texture path or pattern"
                                 : "form editor ID or plugin form key",
      Studio::Width::Px(320.0f), a_frame.scale);
  const bool nameValid =
      IsName(name) && std::ranges::find(a_frame.snapshot->loaded, name) ==
                          a_frame.snapshot->loaded.end();
  const bool ready = nameValid && !operand.empty();
  Dim("Creates an empty document with the chosen key. Add outputs to make an "
      "effect.");
  Disabled(!ready, [&] {
    if (ImGui::Button("Create document")) {
      RecipeKey key;
      key.kind = kind;
      if (kind == KeyKind::kMaterial) {
        key.operand = std::string{operand};
      } else {
        key.operand = FormRef::From(operand);
      }
      Studio::Post(*a_frame.intents, Studio::CreateRecipe{name, key, {}});
      ImGui::CloseCurrentPopup();
    }
  });
  CreateForSelectedArmor(a_frame, name, nameValid);
  ImGui::EndPopup();
}

void RenameRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("Rename", ImVec2{ButtonWidth("Rename"), 0.0f})) {
    ImGui::OpenPopup("rename-recipe");
  }
  if (ImGui::BeginPopup("rename-recipe")) {
    const std::string_view typed = LiveTextField(
        "rename", a_frame.recipe->id.c_str(), Studio::Width::Px(240.0f), 1.0f);
    const bool ready = !typed.empty() && typed != a_frame.recipe->id;
    ImGui::SameLine();
    Disabled(!ready, [&]() {
      if (ImGui::Button("Rename##do") && ready) {
        Studio::Post(
            *a_frame.intents,
            Studio::RenameRecipe{a_frame.recipe->id, std::string{typed}});
        ImGui::CloseCurrentPopup();
      }
    });
    ImGui::EndPopup();
  }
  Tooltip("rename the recipe; its file follows when it is the user's");
}

}

void DrawStudioContext(const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.intents) {
    return;
  }
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Session", .buttons = {}}));
  DrawRecipeContext(a_frame);
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Recipe", .buttons = {}}));
  NewRecipeButton(a_frame);
  if (!a_frame.recipe) {
    return;
  }
  ImGui::SameLine();
  UndoRedoButtons(*a_frame.recipe, *a_frame.intents);
  ImGui::SameLine();
  RenameRecipeButton(a_frame);
  ImGui::SameLine();
  if (ImGui::Button("Recipe keys")) {
    ImGui::OpenPopup("recipe-keys");
  }
  KeysPopup(a_frame.piece ? *a_frame.piece : Studio::PieceRow{},
            *a_frame.recipe, *a_frame.intents);
}

void DrawRecipeSettings(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.intents || !a_frame.names) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  [[maybe_unused]] const std::optional<std::size_t> detail =
      DrawForm("recipe-header", Studio::RecipeHeaderForm(recipe), a_frame);
  if (!recipe.problems.empty()) {
    static_cast<void>(Rule(Studio::RuleSpec{.text = "Rows with problems"}));
    if (recipe.heldBack) {
      Problem("Held back: recipe-level errors keep it out of the applied "
              "set until they are fixed");
    }
    for (const Diagnostic &diagnostic : recipe.problems) {
      const std::string line =
          std::format("{}: {}", diagnostic.where, diagnostic.message);
      if (diagnostic.severity == Severity::kError) {
        Problem(line);
      } else {
        Warn(line);
      }
    }
  }
}

void DrawOutputHeader(const Studio::OutputRow &a_output,
                      std::span<const Studio::FormField> a_scalars,
                      const Frame &a_frame) {
  const Studio::OutputHeader header = Studio::OutputHeaderForm(
      a_output.index, a_output.replace, a_output.selection);
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Inspector"}, RowButtonWidth(), [&]() {
        if (a_frame.recipe && a_frame.intents && RemoveButton(0)) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveOutput{a_output.index});
        }
      }));
  Dim(std::format("{} / {}", SurfaceName(a_output.surface),
                  SlotName(a_output.slot)));
  std::vector<Studio::FormField> settings(a_scalars.begin(), a_scalars.end());
  settings.insert(settings.end(), header.fields.begin(), header.fields.end());
  DrawFormWithSignals("settings", settings, a_frame);
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Applies to"}));
  DrawSelector(header.selector, a_output.index, false, a_frame);
}
}
