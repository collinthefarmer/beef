#include "menu/StudioPage.h"
#include "diagnostics/Trace.h"

#include "menu/BoardPage.h"
#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/Frame.h"
#include "menu/Menu.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/RecipeActions.h"
#include "menu/ResourcePanels.h"
#include "menu/StackPanel.h"
#include "menu/Tuning.h"
#include "menu/Workspace.h"

#include "engine/Manager.h"
#include "studio/Board.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/PaintSession.h"
#include "studio/Panels.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/View.h"
#include "studio/Widgets.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
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
constexpr Studio::TableStyle kFooterStyle{.borders = Studio::TableBorders::kAll,
                                          .stretch = true,
                                          .headers = true,
                                          .rowBackground = false};

void DrawApplication(const Frame &a_frame) {
  const auto &selection = SelectionOf(a_frame);
  const std::string_view recipeID = Studio::MaskTaskActive(*a_frame.state)
                                        ? Studio::kPaintRecipe
                                        : std::string_view{selection.recipeID};
  const ApplicationRecord *latest = nullptr;
  for (const ApplicationRecord &record : a_frame.snapshot->applications) {
    if (record.token.actorID != 0 && selection.piece.actorID != 0 &&
        record.token.actorID != selection.piece.actorID) {
      continue;
    }
    if (!record.token.recipeID.empty() && record.token.recipeID != recipeID) {
      continue;
    }
    if (!latest || record.token.revision > latest->token.revision) {
      latest = &record;
    }
  }
  if (!latest) {
    return;
  }
  ApplicationPhase phase = latest->phase;
  std::string problem = latest->problem;
  for (const ApplicationActor &actor : latest->actors) {
    if (actor.actorID != selection.piece.actorID) {
      continue;
    }
    phase = actor.phase;
    problem = actor.problem;
    break;
  }
  Dim(std::format("Application: {}", ApplicationPhaseName(phase)));
  if (!problem.empty()) {
    Problem(problem);
  }
}

void DrawBody(const Frame &a_frame) {
  DrawApplication(a_frame);
  DrawPaintDraftBar(a_frame);
  if (!a_frame.recipe) {
    if (a_frame.state->paint) {
      Dim("The draft destination is unavailable. Resume it after restoring the "
          "recipe, or discard it.");
    } else {
      Dim("Choose a loaded recipe or create a new document above. The selected "
          "recipe may still be loading or may have been removed.");
    }
    return;
  }
  ImGui::PushID(a_frame.recipe->id.c_str());
  if (a_frame.geometry) {
    DrawWorkspace(a_frame);
  } else {
    DrawWorkspace(a_frame);
    Rule();
    Dim(std::format("recipe {} is bound to no geometry of this piece: its keys "
                    "or selectors match none of its geometries",
                    a_frame.recipe->id));
  }
  ImGui::PopID();
}

void ReturnToLive() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  manager->Editor().UpdateView([](Studio::View &a_live) {
    a_live.isolation = {};
    a_live.muted.clear();
    a_live.freeze = false;
    a_live.speed = 1.0f;
  });
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

void DrawAuditionBar(const Frame &a_frame) {
  const Studio::View &view = ViewOf(a_frame);
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Audition", .buttons = {}}));
  if (a_frame.recipe) {
    IsolateCheckbox(*a_frame.recipe, view, "Solo recipe", *a_frame.intents);
  }
  if (view.Isolating()) {
    Warn(std::format("Solo: {}", view.isolation.recipeID));
  }
  if (!view.muted.empty()) {
    Warn(std::format("{} muted layer(s)", view.muted.size()));
  }
}

void DrawClock(const Frame &a_frame) {
  const Studio::RecipeRow *recipe = a_frame.recipe;
  const Studio::View &view = ViewOf(a_frame);
  Studio::Intents &out = *a_frame.intents;
  const float now = recipe ? recipe->time : 0.0f;
  Table table = Table::Begin("footer",
                             {{"freeze", Studio::Width::Fit()},
                              {"step", Studio::Width::Fit()},
                              {"speed", Studio::Width::Fit()},
                              {"t (s)", Studio::Width::Fill()}},
                             kFooterStyle);
  if (!table.Open()) {
    return;
  }
  table.Cell();
  bool freeze = view.freeze;
  if (Toggle("##freeze", freeze, "")) {
    Studio::Post(out, Studio::SetFreeze{freeze, now});
  }
  table.Cell();
  if (ImGui::SmallButton(">|")) {
    if (!view.freeze) {
      Studio::Post(out, Studio::SetFreeze{true, now});
    }
    Studio::Post(out, Studio::StepClock{});
  }
  Tooltip("advance the recipe one tick and hold");
  table.Cell();
  float speed = view.speed;
  NextItemWidth(Studio::Width::Px(120.0f));
  if (ImGui::SliderFloat("##speed", &speed, 0.0f, 4.0f, "%.2fx")) {
    Studio::Post(out, Studio::SetSpeed{speed});
  }
  Tooltip("multiplies every recipe's clock");
  table.Cell();
  const float actual = view.freeze ? view.scrubSeconds : now;
  const float minute = std::floor(actual / 60.0f) * 60.0f;
  float shown = actual - minute;
  NextItemWidth(Studio::Width::Fill());
  const bool changed =
      ImGui::SliderFloat("##scrub", &shown, 0.0f, 60.0f, "%.2f");
  if (changed || ImGui::IsItemActive()) {
    Studio::Post(out, Studio::SetScrub{minute + shown});
  }
  if (minute > 0.0f) {
    Tooltip(std::format("minute {} of the clock; t = {:.2f} s",
                        static_cast<int>(minute / 60.0f) + 1, actual));
  }
  table.End();
}

void DrawFooter(const Frame &a_frame) {
  const Studio::View &view = ViewOf(a_frame);
  DrawAuditionBar(a_frame);
  static_cast<void>(Rule(Studio::RuleSpec{.text = "Session", .buttons = {}}));
  Disabled(Studio::MaskTaskActive(*a_frame.state), [&] {
    if (ImGui::SmallButton("Return to live")) {
      ReturnToLive();
    }
  });
  Tooltip("clear solo and mute, resume the global clock at normal speed");
  if (view.freeze || view.speed != 1.0f) {
    Dim(std::format("Clock: {} at {:.2f}x", view.freeze ? "held" : "running",
                    view.speed));
  }
  DrawClock(a_frame);
}

void HistoryKeys(const Frame &a_frame) {
  const Studio::RecipeRow *recipe = a_frame.recipe;
  const Studio::MenuState &state = *a_frame.state;
  if (state.paint && state.paint->pendingCommit) {
    return;
  }
  const auto *io = ImGui::GetIO();
  if (!recipe || !io || !io->KeyCtrl || state.activeField != Studio::kNoField) {
    return;
  }
  const bool undo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Z, false);
  const bool redo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Y, false);
  Studio::Intents &out = *a_frame.intents;
  if (Studio::MaskTaskActive(state)) {
    if (undo) {
      Studio::Post(out, Studio::UndoMask{});
    }
    if (redo) {
      Studio::Post(out, Studio::RedoMask{});
    }
    return;
  }
  if (undo) {
    Studio::Post(out, Studio::Undo{recipe->id});
  }
  if (redo) {
    Studio::Post(out, Studio::Redo{recipe->id});
  }
}

void TraceStudioSelection(const Studio::Selection &a_selection) {
  Trace::Page("Studio",
              std::format("actor={:08X} armor={:08X} camera={} recipe={}",
                          a_selection.piece.actorID, a_selection.piece.armorID,
                          a_selection.piece.firstPerson ? "1st" : "3rd",
                          a_selection.recipeID));
}

void DrawStudioFrame(const Frame &frame) {
  Studio::MenuState &state = *frame.state;
  const Studio::Snapshot &snapshot = *frame.snapshot;
  const bool editPending = state.pendingIndexedEdit.has_value() ||
                           state.pendingRecipeFile.has_value() ||
                           RecipeFilePending(frame);
  Disabled(editPending || (state.paint && state.paint->pendingCommit), [&] {
    DrawStudioContext(frame);
    DrawRecipeFileActions(frame);
  });
  if (editPending) {
    Dim("Waiting for the recipe change.");
  }
  const Studio::View &view = snapshot.view;
  const float footerRows = 4.0f + (view.Isolating() ? 1.0f : 0.0f) +
                           (!view.muted.empty() ? 1.0f : 0.0f) +
                           ((view.freeze || view.speed != 1.0f) ? 1.0f : 0.0f);
  const float footer = RuleHeight() * 2.0f +
                       ImGui::GetFrameHeightWithSpacing() * footerRows + 8.0f;
  if (ImGui::BeginChild("studio-body", ImVec2{0.0f, -footer}, 0, 0)) {
    Disabled(editPending || (state.paint && state.paint->pendingCommit),
             [&] { DrawBody(frame); });
  }
  ImGui::EndChild();
  DrawFooter(frame);
  if (!editPending) {
    HistoryKeys(frame);
  }
}

}

void __stdcall RenderStudio() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = Studio::State();
  TraceStudioSelection(state.selection);
  manager->Watch(Studio::RequestOf(state.selection),
                 state.selection.document ? state.selection.recipeID : "");
  const std::shared_ptr<const Manager::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    return;
  }
  const Studio::Snapshot &snapshot = *held;
  if (snapshot.paintUpdate) {
    Studio::AcknowledgePaintUpdate(state, *snapshot.paintUpdate);
  }
  if (snapshot.paintCommit) {
    Studio::AcknowledgePaintCommit(state, *snapshot.paintCommit);
  }
  Studio::ResolveEditorSelection(state, snapshot);
  Studio::AcknowledgeEditorOperations(state, snapshot);
  BeginTuningFrame(state, snapshot);
  Studio::Intents intents;

  const Studio::PieceRow *piece =
      Studio::SelectedPiece(snapshot, state.selection);
  const Studio::RecipeRow *recipe =
      Studio::SelectedRecipe(snapshot, state.selection);
  [[maybe_unused]] const bool resolved =
      Studio::ResolveInspectorSubject(state.selection, recipe);
  Studio::ObservePaintRecipe(state, recipe);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(recipe, state.selection);
  const Studio::Names names =
      recipe ? Studio::NamesOf(*recipe,
                               geometry ? *geometry : Studio::GeometryRow{})
             : Studio::Names{};

  const Frame frame{
      .snapshot = &snapshot,
      .piece = piece,
      .recipe = recipe,
      .geometry = geometry,
      .names = &names,
      .state = &state,
      .intents = &intents,
  };

  DrawStudioFrame(frame);
  EndTuningFrame(state);
  Dispatch(intents, state, snapshot);
  RebuildScratch(frame);
  Dispatch(intents, state, snapshot);
}

void __stdcall RenderBoard() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = Studio::State();
  manager->Watch(Studio::RequestOf(state.selection),
                 state.selection.document ? state.selection.recipeID : "");
  const std::shared_ptr<const Manager::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    return;
  }
  const Studio::Snapshot &snapshot = *held;
  Studio::Intents intents;
  const Studio::PieceRow *piece =
      Studio::SelectedPiece(snapshot, state.selection);
  const Studio::RecipeRow *recipe =
      Studio::SelectedRecipe(snapshot, state.selection);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(recipe, state.selection);
  const Studio::Names names =
      recipe ? Studio::NamesOf(*recipe,
                               geometry ? *geometry : Studio::GeometryRow{})
             : Studio::Names{};
  const Frame frame{
      .snapshot = &snapshot,
      .piece = piece,
      .recipe = recipe,
      .geometry = geometry,
      .names = &names,
      .state = &state,
      .intents = &intents,
  };
  if (recipe) {
    DrawBoardPage(frame);
  } else {
    Dim("Select a recipe in Studio to see its composition.");
  }
  Dispatch(intents, state, snapshot);
}
}
