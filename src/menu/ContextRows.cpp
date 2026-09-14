#include "menu/ContextRows.h"

#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "recipe/Recipe.h"
#include "studio/Board.h"
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
#include <functional>
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

void IsolateCheckbox(const Studio::RecipeRow &a_recipe,
                     const Studio::View &a_view, const char *a_label,
                     Studio::Intents &a_out) {
  bool isolating = a_view.Isolating();
  std::string text;
  if (isolating) {
    text = "isolating " + a_view.isolation.recipeID;
    if (a_view.isolation.output.has_value()) {
      text += std::format(" output {}", *a_view.isolation.output);
    }
    if (a_view.isolation.layer.has_value()) {
      text += std::format(" layer {}", *a_view.isolation.layer);
    }
  }
  if (Toggle(a_label, isolating, text)) {
    Studio::Post(a_out, Studio::SoloRecipe{a_recipe.id, isolating});
  }
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

void ClearButton(const Studio::RecipeRow &a_recipe,
                 std::optional<std::size_t> a_output, Studio::Intents &a_out) {
  Disabled(!a_output, [&]() {
    if (ImGui::Button("Clear", ImVec2{ButtonWidth("Clear"), 0.0f}) &&
        a_output) {
      Studio::Post(a_out, a_recipe.id, Studio::RemoveOutput{*a_output});
    }
  });
  Tooltip("remove the picked slot's output and every layer in it");
}

[[nodiscard]] std::string SlotLabel(const Studio::Cell &a_cell) {
  std::string name{SlotName(a_cell.slot)};
  switch (a_cell.state) {
  case Studio::CellState::kWritten:
    return std::format("{} ({} layer{})", name, a_cell.layers,
                       a_cell.layers == 1 ? "" : "s");
  case Studio::CellState::kRefused:
    return name + " (refused)";
  case Studio::CellState::kExcluded:
    return name + " (excluded)";
  case Studio::CellState::kEmpty:
    return name + " (empty)";
  case Studio::CellState::kAbsent:
    return name;
  }
  return name;
}

void TargetChoice(const Studio::Selection &a_selection,
                  Studio::Intents &a_out) {
  constexpr Target targets[]{Target::kMaterial, Target::kShell, Target::kLight};
  if (!ImGui::BeginCombo("##target",
                         std::string{TargetName(a_selection.target)}.c_str())) {
    return;
  }
  for (const Target target : targets) {
    if (ImGui::Selectable(std::string{TargetName(target)}.c_str(),
                          target == a_selection.target)) {
      Studio::Post(a_out, Studio::PickTarget{target});
    }
  }
  ImGui::EndCombo();
}

void DrawSlotPick(const Studio::Cell &a_cell, bool a_selected,
                  Studio::Intents &a_out) {
  if (!ImGui::Selectable(SlotLabel(a_cell).c_str(), a_selected)) {
    return;
  }
  if (a_cell.output) {
    Studio::Post(a_out, Studio::PickCell{a_cell.surface, a_cell.slot,
                                         a_cell.layers > 0
                                             ? std::optional{a_cell.layers - 1}
                                             : std::nullopt});
  } else {
    Studio::Post(a_out, Studio::PickSlot{a_cell.slot});
  }
}

void SlotChoice(const Studio::Board &a_board, Surface a_surface,
                const Studio::Cell *a_picked, Studio::Intents &a_out) {
  if (!ImGui::BeginCombo("##slot", a_picked ? SlotLabel(*a_picked).c_str()
                                            : "choose a slot")) {
    return;
  }
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    const Studio::Cell *cell =
        Studio::CellAt(a_board, a_surface, static_cast<Slot>(i));
    if (!cell || cell->state == Studio::CellState::kAbsent) {
      continue;
    }
    ImGui::PushID(static_cast<int>(i));
    const bool excluded = cell->state == Studio::CellState::kExcluded;
    Disabled(excluded, [&]() { DrawSlotPick(*cell, cell == a_picked, a_out); });
    if (!cell->reason.empty()) {
      Tooltip(cell->reason);
    }
    ImGui::PopID();
  }
  ImGui::EndCombo();
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

void DrawEditContext(const Studio::Board &a_board, const Studio::Cell *a_picked,
                     const Frame &a_frame) {
  const Studio::RecipeRow &a_recipe = *a_frame.recipe;
  const Studio::Selection &a_selection = SelectionOf(a_frame);
  Studio::Intents &a_out = *a_frame.intents;

  const bool light = a_selection.target == Target::kLight;
  auto table = Table::Begin("context",
                            {{"S", Studio::Width::Px(RowButtonWidth())},
                             {"target", Studio::Width::Fit()},
                             {"slot", Studio::Width::Fit()}},
                            kContextStyle);
  if (!table.Open()) {
    return;
  }
  table.Cell();
  {
    const std::optional<std::size_t> output =
        light ? a_board.light.output
              : (a_picked ? a_picked->output : std::nullopt);
    bool solo =
        light ? a_board.light.isolated : (a_picked && a_picked->isolated);
    Disabled(!output, [&]() {
      if (SoloButton(solo) && output) {
        Studio::Post(a_out, Studio::SoloOutput{a_recipe.id, *output, solo});
      }
    });
  }
  table.Cell();
  NextItemWidth(Studio::Width::Fit("material"));
  TargetChoice(a_selection, a_out);
  table.Cell();
  if (light) {
    Dim("the recipe's light");
  } else {
    NextItemWidth(Studio::Width::Fit(a_picked ? SlotLabel(*a_picked)
                                              : std::string{"choose a slot"}));
    SlotChoice(a_board, SurfaceOf(a_selection.target), a_picked, a_out);
  }
  table.End();
}

void DrawRuleTitle(std::string_view a_title, float a_rightWidth,
                   const std::function<void()> &a_right) {
  ImGui::Dummy(ImVec2{0.0f, ImGui::GetFrameHeight()});
  ImGui::Separator();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(a_title.data(), a_title.data() + a_title.size());
  if (a_rightWidth <= 0.0f) {
    return;
  }
  ImGui::SameLine();
  ImGui::PushID(a_title.data(), a_title.data() + a_title.size());
  RightAligned(a_rightWidth, a_right);
  ImGui::PopID();
}
}

PaneChoice ChoosePane(Target a_target, bool a_settingsWanted) noexcept {
  PaneChoice choice;
  choice.hasSettings = a_target != Target::kMaterial;
  choice.hasStack = a_target != Target::kLight;
  choice.settings =
      choice.hasStack ? (a_settingsWanted && choice.hasSettings) : true;
  return choice;
}

const Studio::Cell *PickedCell(const Studio::Board &a_board,
                               const Studio::Selection &a_selection) noexcept {
  if (a_selection.target == Target::kLight || !a_selection.slot) {
    return nullptr;
  }
  return Studio::CellAt(a_board, SurfaceOf(a_selection.target),
                        *a_selection.slot);
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

void DrawRecipeHeader(const Frame &a_frame) {
  const Studio::PieceRow &piece = *a_frame.piece;
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  Studio::Intents &out = *a_frame.intents;
  const float newWidth = ButtonWidth("New");
  const float renameWidth = ButtonWidth("Rename");
  const float clearWidth = ButtonWidth("Clear");
  const float keysWidth = ButtonWidth("keys");
  const float undoWidth = ButtonWidth("Undo");
  const float redoWidth = ButtonWidth("Redo");
  const float recipeRight = newWidth + renameWidth + clearWidth + keysWidth +
                            undoWidth + redoWidth + 5.0f * ItemSpacingX();
  DrawRuleTitle("Recipe", recipeRight, [&]() {
    NewRecipeButton(a_frame);
    ImGui::SameLine();
    RenameRecipeButton(a_frame);
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2{clearWidth, 0.0f})) {
      Studio::Post(out, recipe.id, Studio::ClearRecipe{});
    }
    Tooltip("empty the recipe: every output, the shell settings, and every "
            "signal, curve, source, mask and variant go; its name and keys "
            "stay");
    ImGui::SameLine();
    if (ImGui::Button("keys", ImVec2{keysWidth, 0.0f})) {
      ImGui::OpenPopup("recipe-keys");
    }
    KeysPopup(piece, recipe, out);
    Tooltip("which pieces the recipe applies to");
    ImGui::SameLine();
    UndoRedoButtons(recipe, out);
  });
  DrawRecipeContext(a_frame);

  DrawRecipeSettings(a_frame);
}

void DrawLightContext(const Studio::Board &a_board, const Frame &a_frame) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  if (!recipe.lightRow.present) {
    if (ImGui::Button("Add light")) {
      Studio::Post(*a_frame.intents, recipe.id, Studio::AddLight{});
    }
    return;
  }
  ImGui::TextUnformatted(a_board.light.description.c_str());
  if (recipe.lightRow.present) {
    DrawSelector(Studio::SelectorViewOf(recipe.lightRow.selection),
                 recipe.lightRow.output, true, a_frame);
  }
}

void DrawSurfaceContext(const Studio::Cell &a_picked, const Frame &a_frame) {
  if (a_picked.state == Studio::CellState::kEmpty) {
    if (ImGui::Button("Add output")) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id,
                   Studio::AddOutput{a_picked.surface, a_picked.slot, {}});
    }
    Tooltip(std::format("add an empty stack on {} of the {}",
                        SlotName(a_picked.slot),
                        SurfaceName(a_picked.surface)));
    return;
  }
  if (!a_picked.reason.empty()) {
    Warn(a_picked.reason);
  }
  if (!a_picked.output) {
    return;
  }
  const auto &outputs = a_frame.geometry->outputs;
  const auto output =
      std::ranges::find(outputs, *a_picked.output, &Studio::OutputRow::index);
  if (output != outputs.end()) {
    DrawOutputHeader(output->index, output->replace, output->selection,
                     a_frame);
  }
}
}

const Studio::Cell *DrawContext(const Studio::Board &a_board,
                                const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.piece || !a_frame.recipe ||
      !a_frame.geometry || !a_frame.state || !a_frame.intents) {
    return nullptr;
  }
  const Studio::Selection &selection = SelectionOf(a_frame);
  const Studio::Cell *picked = PickedCell(a_board, selection);
  DrawRecipeHeader(a_frame);
  const std::optional<std::size_t> output =
      selection.target == Target::kLight
          ? a_board.light.output
          : (picked ? picked->output : std::nullopt);
  DrawRuleTitle("Output", ButtonWidth("Clear"), [&]() {
    ClearButton(*a_frame.recipe, output, *a_frame.intents);
  });
  DrawEditContext(a_board, picked, a_frame);
  if (selection.target == Target::kLight) {
    DrawLightContext(a_board, a_frame);
    return nullptr;
  }
  if (picked) {
    DrawSurfaceContext(*picked, a_frame);
  }
  return picked;
}

void DrawStudioContext(const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.intents) {
    return;
  }
  DrawRecipeContext(a_frame);
  NewRecipeButton(a_frame);
  if (!a_frame.recipe) {
    return;
  }
  IsolateCheckbox(*a_frame.recipe, a_frame.snapshot->view, "Solo recipe",
                  *a_frame.intents);
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
  if (ImGui::Button("Output settings")) {
    ImGui::OpenPopup("Output settings");
  }
  DetailModal("Output settings", [&]() {
    for (const Studio::OutputRow &output : recipe.outputs) {
      ImGui::PushID(static_cast<int>(output.index));
      ImGui::SeparatorText(
          std::format("Output {}: {}", output.index, TargetName(output.target))
              .c_str());
      if (output.target == Target::kLight) {
        const Studio::FormField replace = Studio::ToggleField(
            "replace", output.replace, Studio::BindLightReplace(output.index));
        DrawRowField("light-replace", replace, a_frame);
        DrawSelector(Studio::SelectorViewOf(output.selection), output.index,
                     true, a_frame);
      } else {
        DrawOutputHeader(output.index, output.replace, output.selection,
                         a_frame);
      }
      ImGui::PopID();
    }
  });
}

namespace {
void ApplyDefaultsButton(const Studio::Board &a_board, const Frame &a_frame) {
  const bool light = SelectionOf(a_frame).target == Target::kLight;
  const bool present =
      !light || (a_board.light.present && a_board.light.output.has_value());
  Disabled(!present, [&]() {
    if (!ImGui::Button("Apply Defaults",
                       ImVec2{ButtonWidth("Apply Defaults"), 0.0f}) ||
        !present) {
      return;
    }
    if (light) {
      Studio::Post(*a_frame.intents, a_frame.recipe->id,
                   Studio::ResetLight{*a_board.light.output});
    } else {
      Studio::Post(*a_frame.intents, a_frame.recipe->id, Studio::ResetShell{});
    }
  });
}
}

void DrawPaneRule(std::string_view a_title, const PaneChoice &a_pane,
                  const Studio::Board &a_board, const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents) {
    return;
  }
  const float switchWidth =
      (std::max)(ButtonWidth("settings"), ButtonWidth("stack"));
  const float rightWidth =
      switchWidth +
      (a_pane.settings ? ButtonWidth("Apply Defaults") + ItemSpacingX() : 0.0f);
  DrawRuleTitle(a_title, rightWidth, [&]() {
    if (a_pane.settings) {
      ApplyDefaultsButton(a_board, a_frame);
      ImGui::SameLine();
    }
    const bool enabled = a_pane.settings ? a_pane.hasStack : a_pane.hasSettings;
    Disabled(!enabled, [&]() {
      if (ImGui::Button(a_pane.settings ? "stack" : "settings",
                        ImVec2{switchWidth, 0.0f}) &&
          enabled) {
        Studio::Post(*a_frame.intents, Studio::ShowSettings{!a_pane.settings});
      }
    });
  });
}

void DrawOutputHeader(std::size_t a_output, bool a_replace,
                      const Selector &a_selector, const Frame &a_frame) {
  const Studio::OutputHeader header =
      Studio::OutputHeaderForm(a_output, a_replace, a_selector);
  [[maybe_unused]] const std::optional<std::size_t> detail =
      DrawForm("output-header", header.fields, a_frame);
  DrawSelector(header.selector, a_output, false, a_frame);
}
}
