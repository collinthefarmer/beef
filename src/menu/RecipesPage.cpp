#include "menu/Menu.h"

#include "engine/Manager.h"
#include "engine/RecipeStore.h"
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

void DrawLoadedTable() {
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
  for (const Recipe &recipe : LoadedRecipes()) {
    if (IsTransient(recipe.id)) {
      continue;
    }
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
                recipe.signals.size(), recipe.curves.size(),
                recipe.sources.size(), recipe.masks.size(),
                recipe.outputs.size());
    table.Cell();
    const auto origin = OriginOf(recipe);
    std::size_t errors = 0;
    std::size_t warnings = 0;
    if (origin) {
      for (const auto &diagnostic : origin->diagnostics) {
        (diagnostic.severity == Severity::kError ? errors : warnings)++;
      }
    }
    if (errors) {
      Problem(std::format("{} error(s)", errors));
    } else if (warnings) {
      Warn(std::format("{} warning(s)", warnings));
    } else {
      Ok("ok");
    }
    if (!recipe.metadata.imported.empty()) {
      ImGui::SameLine();
      Dim("imported, not yet edited");
    }
    table.Cell();
    ImGui::TextWrapped("%s", origin ? origin->path.string().c_str() : "");
  }
  table.End();
}

void DrawResolved(const Studio::PieceRow &a_piece) {
  for (const Studio::RecipeRow &recipe : a_piece.recipes) {
    ImGui::BulletText("%s  by %s  priority %d  t %.1fs  %zu geometr%s%s%s",
                      recipe.id.c_str(), recipe.key.c_str(), recipe.priority,
                      recipe.time, recipe.geometries.size(),
                      recipe.geometries.size() == 1 ? "y" : "ies",
                      recipe.light.empty() ? "" : "  ", recipe.light.c_str());
    for (const Studio::GeometryRow &geometry : recipe.geometries) {
      std::string outputs;
      for (const Studio::OutputRow &output : geometry.outputs) {
        outputs +=
            std::format("{}{}->{}{}", outputs.empty() ? "" : ", ",
                        output.target == Target::kLight ? std::string_view{}
                                                        : SlotName(output.slot),
                        TargetName(output.target),
                        output.problem.empty()
                            ? (output.animated ? " (animated)" : " (static)")
                            : std::format(" [{}]", output.problem));
      }
      ImGui::Indent();
      ImGui::TextWrapped(
          "%s  [%s]%s%s  %s",
          Studio::GeometryLabel(geometry.name, a_piece.armorName).c_str(),
          geometry.privateMaterial ? "private material" : "material untouched",
          geometry.shell.empty() ? "" : "  ", geometry.shell.c_str(),
          outputs.c_str());
      ImGui::Unindent();
    }
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
  const Studio::Snapshot &snapshot = *held;
  Studio::ResolveSelection(state.selection, snapshot);
  RenderHeader(snapshot);
  const Studio::Selection &selection = state.selection;

  if (ImGui::Button("Reload recipes")) {
    Studio::Reduce(state, Studio::EndPaint{});
    manager->ReloadRecipes();
  }
  ImGui::SameLine();
  if (ImGui::Button("Re-apply all")) {
    manager->ReapplyAll();
  }
  ImGui::SameLine();
  if (ImGui::Button("Retire all (baseline)")) {
    manager->RetireAll();
  }

  ImGui::SeparatorText("Loaded");
  DrawLoadedTable();

  const Studio::PieceRow *piece = Studio::SelectedPiece(snapshot, selection);
  ImGui::SeparatorText("Resolved for the selection (merge order)");
  if (!piece) {
    ImGui::TextDisabled(
        "nothing applied; equip enchanted PBR armor or press Re-apply all");
    return;
  }
  DrawResolved(*piece);

  ImGui::SeparatorText("Board: what the selected recipe writes");
  const Studio::RecipeRow *selected = Studio::SelectedRecipe(piece, selection);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(selected, selection);
  Studio::Names names;
  if (selected && geometry) {
    names = Studio::NamesOf(*selected, *geometry);
  }
  Studio::Intents intents;
  Frame frame{};
  frame.snapshot = &snapshot;
  frame.piece = piece;
  frame.recipe = selected;
  frame.geometry = geometry;
  frame.names = &names;
  frame.state = &state;
  frame.intents = &intents;
  DrawBoardPage(frame);
  Dispatch(intents, state, snapshot);

  if (!selected) {
    return;
  }
  ImGui::SeparatorText(
      selected->dirty
          ? std::format("{} (edited, not saved)", selected->id).c_str()
          : selected->id.c_str());
  if (ImGui::Button("Save")) {
    manager->SaveRecipe(selected->id);
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert to file")) {
    manager->RevertRecipe(selected->id);
  }
  ImGui::SameLine();
  HelpMarker("Save writes the recipe to its file. An imported recipe is saved "
             "to user/<id>.json with its imported line dropped, and loads from "
             "there afterwards.");
  if (!selected->problems.empty()) {
    ImGui::SeparatorText("Rows with problems");
    for (const Diagnostic &diagnostic : selected->problems) {
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
}
