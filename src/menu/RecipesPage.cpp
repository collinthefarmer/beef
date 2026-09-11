#include "menu/Menu.h"

#include "engine/Manager.h"
#include "menu/BoardPage.h"
#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "recipe/Recipe.h"
#include "studio/Intent.h"
#include "studio/Names.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/Widgets.h"

#include <cstddef>
#include <format>
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
constexpr Studio::TableStyle kGridStyle{.borders = Studio::TableBorders::kAll,
                                        .stretch = true,
                                        .headers = true,
                                        .rowBackground = true};

void DrawLoadedTable(const Studio::Snapshot &a_snapshot) {
  auto table = Table::Begin("recipes",
                            {{"recipe", Studio::Width::Fill()},
                             {"keys", Studio::Width::Fill()},
                             {"rows", Studio::Width::Fill()},
                             {"state", Studio::Width::Fill()},
                             {"file", Studio::Width::Fill()}},
                            kGridStyle);
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

[[nodiscard]] std::string OutputDescription(const Studio::OutputRow &a_output) {
  const std::string state =
      a_output.problem.empty()
          ? (a_output.animated ? " (animated)" : " (static)")
          : std::format(" [{}]", a_output.problem);
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
  for (const Studio::RecipeRow &recipe : a_piece.recipes) {
    ImGui::BulletText("%s  by %s  priority %d  t %.1fs  %zu geometr%s%s%s",
                      recipe.id.c_str(), recipe.key.c_str(), recipe.priority,
                      recipe.time, recipe.geometries.size(),
                      recipe.geometries.size() == 1 ? "y" : "ies",
                      recipe.light.empty() ? "" : "  ", recipe.light.c_str());
    for (const Studio::GeometryRow &geometry : recipe.geometries) {
      DrawResolvedGeometry(geometry, a_piece);
    }
  }
}

void DrawStoreActions(Manager &a_manager, Studio::MenuState &a_state) {
  if (ImGui::Button("Reload recipes")) {
    Studio::Reduce(a_state, Studio::EndPaint{});
    a_manager.ReloadRecipes();
  }
  ImGui::SameLine();
  if (ImGui::Button("Re-apply all")) {
    a_manager.ReapplyAll();
  }
  ImGui::SameLine();
  if (ImGui::Button("Retire all (baseline)")) {
    a_manager.RetireAll();
  }
}

void DrawRecipeFile(const Studio::RecipeRow &a_recipe, Manager &a_manager) {
  ImGui::SeparatorText(
      a_recipe.dirty
          ? std::format("{} (edited, not saved)", a_recipe.id).c_str()
          : a_recipe.id.c_str());
  if (ImGui::Button("Save")) {
    a_manager.SaveRecipe(a_recipe.id);
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert to file")) {
    a_manager.RevertRecipe(a_recipe.id);
  }
  ImGui::SameLine();
  HelpMarker("Save writes the recipe to its file. An imported recipe is saved "
             "to user/<id>.json with its imported line dropped, and loads from "
             "there afterwards.");
  if (!a_recipe.problems.empty()) {
    ImGui::SeparatorText("Rows with problems");
    for (const Diagnostic &diagnostic : a_recipe.problems) {
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

void DrawSelection(const Studio::Snapshot &a_snapshot,
                   Studio::MenuState &a_state, Manager &a_manager) {
  const Studio::PieceRow *piece =
      Studio::SelectedPiece(a_snapshot, a_state.selection);
  ImGui::SeparatorText("Resolved for the selection (merge order)");
  if (!piece) {
    ImGui::TextDisabled(
        "nothing applied; equip enchanted PBR armor or press Re-apply all");
    return;
  }
  DrawResolved(*piece);
  ImGui::SeparatorText("Board: what the selected recipe writes");
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
  DrawBoardPage(frame);
  Dispatch(intents, a_state, a_snapshot);
  if (selected) {
    DrawRecipeFile(*selected, a_manager);
  }
}
}

void __stdcall RenderRecipes() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = Studio::State();
  manager->Watch(Studio::RequestOf(state.selection));
  const std::shared_ptr<const Studio::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    return;
  }
  Studio::ResolveSelection(state.selection, *held);
  RenderHeader(*held);
  DrawStoreActions(*manager, state);
  ImGui::SeparatorText("Loaded");
  DrawLoadedTable(*held);
  DrawSelection(*held, state, *manager);
}
}
