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

void RecipeCombo(const Studio::PieceRow &a_piece,
                 const Studio::RecipeRow &a_recipe,
                 std::span<const std::string> a_loaded, const char *a_label,
                 Studio::Intents &a_out) {
  if (!ImGui::BeginCombo(a_label, RecipeLabel(a_recipe).c_str())) {
    return;
  }
  for (const auto &recipe : a_piece.recipes) {
    const std::string label =
        recipe.pinned ? RecipeLabel(recipe)
                      : std::format("{} ({}, priority {})", recipe.id,
                                    recipe.key, recipe.priority);
    if (ImGui::Selectable(label.c_str(), &recipe == &a_recipe)) {
      Studio::Post(a_out, Studio::PickRecipe{recipe.id});
    }
  }
  bool divided = false;
  for (const auto &id : a_loaded) {
    if (std::ranges::find(a_piece.recipes, id, &Studio::RecipeRow::id) !=
        a_piece.recipes.end()) {
      continue;
    }
    if (!divided) {
      ImGui::Separator();
      divided = true;
    }
    if (ImGui::Selectable(std::format("{} (not worn here)", id).c_str(),
                          false)) {
      Studio::Post(a_out, Studio::PinRecipe{id});
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
    text = "isolating " + a_view.isolateRecipe;
    if (a_view.isolateOutput >= 0) {
      text += std::format(" output {}", a_view.isolateOutput);
    }
    if (a_view.isolateLayer >= 0) {
      text += std::format(" layer {}", a_view.isolateLayer);
    }
  }
  if (Toggle(a_label, isolating, text)) {
    Studio::Post(a_out, Studio::SoloRecipe{a_recipe.id, isolating});
  }
}

void KeysPopup(const Studio::PieceRow &a_piece,
               const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  if (!ImGui::BeginPopup("recipe-keys")) {
    return;
  }
  auto table = Table::Begin("keys",
                            {{"key", Studio::Width::Fit()},
                             {"", Studio::Width::Px(RowButtonWidth())}},
                            kFormStyle);
  if (table.Open()) {
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
  const std::string name{SlotName(a_cell.slot)};
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
    Disabled(excluded, [&]() {
      if (ImGui::Selectable(SlotLabel(*cell).c_str(), cell == a_picked)) {
        if (cell->output) {
          Studio::Post(a_out,
                       Studio::PickCell{cell->surface, cell->slot,
                                        cell->layers > 0
                                            ? std::optional{cell->layers - 1}
                                            : std::nullopt});
        } else {
          Studio::Post(a_out, Studio::PickSlot{cell->slot});
        }
      }
    });
    if (!cell->reason.empty()) {
      Tooltip(cell->reason);
    }
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

[[maybe_unused]] [[nodiscard]] std::optional<Studio::ViewGeometry>
NextGeometry(const Studio::RecipeRow &a_recipe,
             const Studio::GeometryRow &a_geometry) {
  const auto &shapes = a_recipe.geometries;
  if (shapes.empty()) {
    return std::nullopt;
  }
  const auto it =
      std::ranges::find(shapes, a_geometry.name, &Studio::GeometryRow::name);
  const std::size_t at =
      it == shapes.end() ? 0 : static_cast<std::size_t>(it - shapes.begin());
  return Studio::ViewGeometry{shapes[(at + 1) % shapes.size()].name};
}

void DrawRecipeContext(const Studio::Snapshot &a_snapshot,
                       const Studio::PieceRow &a_piece,
                       const Studio::RecipeRow &a_recipe,
                       Studio::Intents &a_out) {
  auto table = Table::Begin("recipe-context",
                            {{"S", Studio::Width::Fit()},
                             {"selection", Studio::Width::Fit()},
                             {"recipe", Studio::Width::Fit()},
                             {"history", Studio::Width::Fit()}},
                            kContextStyle);
  if (!table.Open()) {
    return;
  }
  table.Cell();
  IsolateCheckbox(a_recipe, a_snapshot.view, "##isolate", a_out);
  Tooltip("solo the recipe: apply it alone");
  table.Cell();
  NextItemWidth(Studio::Width::Fit(
      std::format("{} / {} (3rd)", a_piece.actorName, a_piece.armorName)));
  SelectionCombo(a_snapshot, &a_piece, "##selection", a_out);
  table.Cell();
  NextItemWidth(Studio::Width::Fit(RecipeLabel(a_recipe)));
  RecipeCombo(a_piece, a_recipe, a_snapshot.loaded, "##recipe", a_out);
  table.End();
}

void DrawEditContext(const Studio::Board &a_board,
                     const Studio::RecipeRow &a_recipe,
                     const Studio::Cell *a_picked,
                     const Studio::Selection &a_selection,
                     Studio::Intents &a_out) {
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

const Studio::Cell *DrawContext(const Studio::Board &a_board,
                                const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.piece || !a_frame.recipe ||
      !a_frame.geometry || !a_frame.state || !a_frame.intents) {
    return nullptr;
  }
  const Studio::Snapshot &snapshot = *a_frame.snapshot;
  const Studio::PieceRow &piece = *a_frame.piece;
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::GeometryRow &geometry = *a_frame.geometry;
  const Studio::Selection &selection = SelectionOf(a_frame);
  Studio::Intents &out = *a_frame.intents;

  const Studio::Cell *picked = PickedCell(a_board, selection);

  const float newWidth = ButtonWidth("New");
  const float renameWidth = ButtonWidth("Rename");
  const float clearWidth = ButtonWidth("Clear");
  const float keysWidth = ButtonWidth("keys");
  const float undoWidth = ButtonWidth("Undo");
  const float redoWidth = ButtonWidth("Redo");
  const float recipeRight = newWidth + renameWidth + clearWidth + keysWidth +
                            undoWidth + redoWidth + 5.0f * ItemSpacingX();
  DrawRuleTitle("Recipe", recipeRight, [&]() {
    const std::optional<RecipeKey> key = DefaultKeyOf(piece);
    Disabled(!key, [&]() {
      if (ImGui::Button("New", ImVec2{newWidth, 0.0f}) && key) {
        std::vector<std::string> ids;
        for (const auto &existing : piece.recipes) {
          ids.push_back(existing.id);
        }
        Studio::Post(out,
                     Studio::CreateRecipe{Studio::UniqueName("recipe", ids),
                                          *key, geometry.name});
      }
    });
    Tooltip("a new recipe keyed to this armor, named recipe-N, with one empty "
            "emissive output on the material for the viewed geometry alone; "
            "rename it and edit its keys from keys");
    ImGui::SameLine();
    if (ImGui::Button("Rename", ImVec2{renameWidth, 0.0f})) {
      ImGui::OpenPopup("rename-recipe");
    }
    if (ImGui::BeginPopup("rename-recipe")) {
      const std::string_view typed = LiveTextField(
          "rename", recipe.id.c_str(), Studio::Width::Px(240.0f), 1.0f);
      const bool ready = !typed.empty() && typed != recipe.id;
      ImGui::SameLine();
      Disabled(!ready, [&]() {
        if (ImGui::Button("Rename##do") && ready) {
          Studio::Post(out,
                       Studio::RenameRecipe{recipe.id, std::string{typed}});
          ImGui::CloseCurrentPopup();
        }
      });
      ImGui::EndPopup();
    }
    Tooltip("rename the recipe; its file follows when it is the user's");
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
  DrawRecipeContext(snapshot, piece, recipe, out);

  const std::optional<std::size_t> outputForClear =
      selection.target == Target::kLight
          ? a_board.light.output
          : (picked ? picked->output : std::nullopt);
  DrawRuleTitle("Output", ButtonWidth("Clear"),
                [&]() { ClearButton(recipe, outputForClear, out); });
  DrawEditContext(a_board, recipe, picked, selection, out);

  if (selection.target == Target::kLight) {
    if (a_board.light.present) {
      ImGui::TextUnformatted(a_board.light.description.c_str());
      if (a_board.light.output) {
        ImGui::SameLine();
        bool solo = a_board.light.isolated;
        if (SoloButton(solo)) {
          Studio::Post(
              out, Studio::SoloOutput{recipe.id, *a_board.light.output, solo});
        }
      }
    } else if (ImGui::Button("Add light")) {
      Studio::Post(out, recipe.id, Studio::AddLight{});
    }
    return nullptr;
  }
  if (!picked) {
    return nullptr;
  }
  if (picked->state == Studio::CellState::kEmpty) {
    if (ImGui::Button("Add output")) {
      Studio::Post(out, recipe.id,
                   Studio::AddOutput{picked->surface, picked->slot, {}});
    }
    Tooltip(std::format("add an empty stack on {} of the {}",
                        SlotName(picked->slot), SurfaceName(picked->surface)));
    return picked;
  }
  if (!picked->reason.empty()) {
    Warn(picked->reason);
  }
  return picked;
}

void DrawPaneRule(std::string_view a_title, const PaneChoice &a_pane,
                  const Studio::Board &a_board, const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.state || !a_frame.intents) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Target target = SelectionOf(a_frame).target;
  Studio::Intents &out = *a_frame.intents;

  const float switchWidth =
      (std::max)(ButtonWidth("settings"), ButtonWidth("stack"));
  const float defaultsWidth = ButtonWidth("Apply Defaults");
  const float rightWidth =
      switchWidth + (a_pane.settings ? defaultsWidth + ItemSpacingX() : 0.0f);
  DrawRuleTitle(a_title, rightWidth, [&]() {
    if (a_pane.settings) {
      const bool light = target == Target::kLight;
      const bool present =
          !light || (a_board.light.present && a_board.light.output.has_value());
      Disabled(!present, [&]() {
        if (ImGui::Button("Apply Defaults", ImVec2{defaultsWidth, 0.0f}) &&
            present) {
          if (light) {
            Studio::Post(out, recipe.id,
                         Studio::ResetLight{a_board.light.output.value_or(0)});
          } else {
            Studio::Post(out, recipe.id, Studio::ResetShell{});
          }
        }
      });
      ImGui::SameLine();
    }
    const bool enabled = a_pane.settings ? a_pane.hasStack : a_pane.hasSettings;
    Disabled(!enabled, [&]() {
      if (ImGui::Button(a_pane.settings ? "stack" : "settings",
                        ImVec2{switchWidth, 0.0f}) &&
          enabled) {
        Studio::Post(out, Studio::ShowSettings{!a_pane.settings});
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
