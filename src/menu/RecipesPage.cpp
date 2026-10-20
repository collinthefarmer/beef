// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Trace.h"
#include "menu/Menu.h"

#include "engine/Manager.h"
#include "menu/BoardPage.h"
#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "menu/PreviewWindows.h"
#include "menu/RecipeActions.h"
#include "recipe/Recipe.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/RecipeSections.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/Widgets.h"

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {

void DrawLoadedTable(const Studio::Snapshot &a_snapshot) {
  auto table = Table::Begin("recipes",
                            {{"recipe", Studio::Width::Fill()},
                             {"keys", Studio::Width::Fill()},
                             {"rows", Studio::Width::Fill()},
                             {"state", Studio::Width::Fill()},
                             {"file", Studio::Width::Fill()}},
                            Studio::kGridTable);
  if (!table.Open()) {
    return;
  }
  for (const Studio::LoadedRecipeRow &recipe : a_snapshot.loadedRecipes) {
    table.Cell();
    ImGui::TextUnformatted(recipe.id.c_str());
    table.Cell();
    std::string keys;
    for (const auto &key : recipe.keys) {
      keys += (keys.empty() ? "" : ", ") + key.ToString();
    }
    ImGui::TextWrapped("%s", keys.c_str());
    table.Cell();
    ImGui::Text("%zu signals, %zu curves, %zu sources, %zu masks, %zu outputs",
                recipe.signals, recipe.curves, recipe.sources, recipe.masks,
                recipe.outputs);
    table.Cell();
    std::size_t errors = 0;
    std::size_t warnings = 0;
    for (const Diagnostic &diagnostic : recipe.diagnostics) {
      (diagnostic.severity == Severity::kError ? errors : warnings)++;
    }
    if (errors) {
      Problem(std::format("{} error(s)", errors));
    } else if (warnings) {
      Warn(std::format("{} warning(s)", warnings));
    } else {
      Ok("ok");
    }
    if (recipe.imported) {
      ImGui::SameLine();
      Dim("imported, not yet edited");
    }
    table.Cell();
    ImGui::TextWrapped("%s", recipe.path.c_str());
  }
  table.End();
}

void DrawUnappliedTable(const Studio::Snapshot &a_snapshot) {
  if (a_snapshot.unappliedRecipes.empty()) {
    return;
  }
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Not applied"}));
  auto table = Table::Begin("unapplied",
                            {{"recipe", Studio::Width::Fill()},
                             {"state", Studio::Width::Fill()},
                             {"problems", Studio::Width::Fill()},
                             {"file", Studio::Width::Fill()}},
                            Studio::kGridTable);
  if (!table.Open()) {
    return;
  }
  for (const Studio::UnappliedRecipeRow &recipe : a_snapshot.unappliedRecipes) {
    table.Cell();
    ImGui::TextUnformatted(recipe.id.c_str());
    table.Cell();
    Problem(Studio::UnappliedReasonName(recipe.reason));
    table.Cell();
    for (const Diagnostic &diagnostic : recipe.diagnostics) {
      const std::string line =
          std::format("{}: {}", diagnostic.where, diagnostic.message);
      if (diagnostic.severity == Severity::kError) {
        Problem(line);
      } else {
        Warn(line);
      }
    }
    table.Cell();
    ImGui::TextWrapped("%s", recipe.path.c_str());
  }
  table.End();
}

[[nodiscard]] std::string OutputStatusText(const Studio::OutputRow &a_output) {
  if (!a_output.problem.empty())
    return std::format(" [{}]", a_output.problem);
  if (!a_output.skipped.empty())
    return std::format(" [skipped: {}]", a_output.skipped);
  return a_output.animated ? " (animated)" : " (static)";
}

[[nodiscard]] std::string OutputDescription(const Studio::OutputRow &a_output) {
  const std::string state = OutputStatusText(a_output);
  return std::format("{}->{}{}",
                     a_output.target == Target::kLight
                         ? std::string_view{}
                         : SlotName(a_output.slot),
                     TargetName(a_output.target), state);
}

void DrawResolvedGeometry(const Studio::GeometryRow &a_geometry,
                          const Studio::PieceRow &a_piece) {
  std::string outputs;
  for (const Studio::OutputRow &output : a_geometry.outputs) {
    outputs += (outputs.empty() ? "" : ", ") + OutputDescription(output);
  }
  ImGui::Indent();
  ImGui::TextWrapped(
      "%s  [%s]%s%s  %s",
      Studio::GeometryLabel(a_geometry.name, a_piece.armorName).c_str(),
      a_geometry.privateMaterial ? "private material" : "material untouched",
      a_geometry.shell.empty() ? "" : "  ", a_geometry.shell.c_str(),
      outputs.c_str());
  ImGui::Unindent();
}

void DrawResolved(const Studio::PieceRow &a_piece) {
  if (a_piece.previewOverride) {
    ImGui::TextDisabled("Preview override active; selection outcomes below "
                        "describe normal matching.");
  }
  for (const RecipeSelection &selection : a_piece.selections) {
    if (selection.outcome != SelectionOutcome::kSelected) {
      const std::string_view outcome = SelectionOutcomeName(selection.outcome);
      ImGui::TextDisabled("%s: %.*s", selection.id.c_str(),
                          static_cast<int>(outcome.size()), outcome.data());
    }
  }
  for (const Studio::RecipeRow &recipe : a_piece.recipes) {
    ImGui::BulletText("%s  by %s  priority %d  t %.1fs  %zu geometr%s%s%s",
                      recipe.id.c_str(), recipe.matchedKey.ToString().c_str(),
                      recipe.priority, recipe.time, recipe.geometries.size(),
                      recipe.geometries.size() == 1 ? "y" : "ies",
                      recipe.light.empty() ? "" : "  ", recipe.light.c_str());
    for (const Studio::GeometryRow &geometry : recipe.geometries) {
      DrawResolvedGeometry(geometry, a_piece);
    }
  }
}

void ReloadRecipes(Manager &a_manager, Studio::MenuState &a_state) {
  a_state.navigation = {};
  a_state.selection.subject = Studio::RecipeSubject{};
  Studio::Reduce(a_state, Studio::EndPaint{});
  a_state.pendingIndexedEdit =
      Studio::PendingIndexedEdit{a_manager.Editor().ReloadRecipes(), {}};
}

void DrawStoreActions(Manager &a_manager, Studio::MenuState &a_state,
                      const Studio::Snapshot &a_snapshot) {
  const std::optional<std::string> unsaved =
      Studio::UnsavedRecipesText(a_snapshot.loadedRecipes);
  if (ImGui::Button("Reload recipes")) {
    if (unsaved) {
      ImGui::OpenPopup("Reload recipes?###reload-recipes");
    } else {
      ReloadRecipes(a_manager, a_state);
    }
  }
  ConfirmModal("Reload recipes?###reload-recipes",
               std::format("Reload discards the unsaved edits to {}, and the "
                           "undo history of every recipe.",
                           unsaved.value_or("")),
               "Reload", [&] { ReloadRecipes(a_manager, a_state); });
  ImGui::SameLine();
  if (ImGui::Button("Re-apply all")) {
    a_manager.ReapplyAll();
  }
  ImGui::SameLine();
  if (ImGui::Button("Retire all (baseline)")) {
    a_manager.RetireAll();
  }
}

void DrawRecipeFile(const Studio::RecipeRow &a_recipe, const Frame &a_frame) {
  static_cast<void>(Rule(Studio::RuleSpec{
      .text = a_recipe.dirty
                  ? std::format("{} (edited, not saved)", a_recipe.id).c_str()
                  : a_recipe.id.c_str()}));
  DrawRecipeFileActions(a_frame);
  HelpMarker("Save writes the recipe to its file. An imported recipe is saved "
             "to user/<id>.json with its imported line dropped, and loads from "
             "there afterwards.");
  DrawDiagnostics(a_recipe.problems, a_recipe.heldBack);
}

void DrawSelection(const Studio::Snapshot &a_snapshot,
                   Studio::MenuState &a_state) {
  const Studio::PieceRow *piece =
      Studio::SelectedPiece(a_snapshot, a_state.selection);
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Resolved for the selection (merge order)"}));
  if (!piece) {
    ImGui::TextDisabled(
        "nothing applied; equip enchanted PBR armor or press Re-apply all");
    return;
  }
  DrawResolved(*piece);
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Board: what the selected recipe writes"}));
  const Studio::RecipeRow *selected =
      Studio::SelectedRecipe(piece, a_state.selection);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(selected, a_state.selection);
  const Studio::Names names = selected && geometry
                                  ? Studio::NamesOf(*selected, *geometry)
                                  : Studio::Names{};
  Studio::Intents intents;
  const Frame frame{.snapshot = &a_snapshot,
                    .piece = piece,
                    .recipe = selected,
                    .geometry = geometry,
                    .names = &names,
                    .state = &a_state,
                    .intents = &intents};
  const bool pending = a_state.pendingIndexedEdit.has_value() ||
                       a_state.pendingRecipeFile.has_value() ||
                       a_state.pendingEditorChange.has_value() ||
                       RecipeFilePending(frame);
  Disabled(pending, [&] { DrawBoardPage(frame); });
  Dispatch(intents, a_state, a_snapshot);
  if (selected) {
    Disabled(pending, [&] { DrawRecipeFile(*selected, frame); });
  }
}
}

void __stdcall RenderRecipes() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = Studio::State();
  const auto &traceSelection = Studio::State().selection;
  Trace::Page("Recipes",
              std::format("actor={:08X} armor={:08X} camera={} recipe={}",
                          traceSelection.piece.actorID,
                          traceSelection.piece.armorID,
                          traceSelection.piece.firstPerson ? "1st" : "3rd",
                          traceSelection.recipeID));
  manager->Watch(Studio::RequestOf(state.selection),
                 state.selection.document ? state.selection.recipeID : "");
  const std::shared_ptr<const Studio::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    RenderPendingStatus();
    return;
  }
  Studio::AcknowledgeEditorOperations(state, *held);
  Studio::ResolveEditorSelection(state, *held);
  if (held->paintUpdate) {
    Studio::AcknowledgePaintUpdate(state, *held->paintUpdate);
  }
  RenderHeader(*held);
  Disabled(state.pendingIndexedEdit.has_value() ||
               state.pendingRecipeFile.has_value() ||
               state.pendingEditorChange.has_value(),
           [&] { DrawStoreActions(*manager, state, *held); });
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Loaded"}));
  DrawLoadedTable(*held);
  DrawUnappliedTable(*held);
  DrawSelection(*held, state);
  DrawPreviewWindows(*held, state, 1.0f);
}
}
