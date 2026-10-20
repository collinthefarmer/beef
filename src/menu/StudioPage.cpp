// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/StudioPage.h"
#include "diagnostics/Trace.h"

#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/Frame.h"
#include "menu/Menu.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/PreviewWindows.h"
#include "menu/RecipeActions.h"
#include "menu/ResourcePanels.h"
#include "menu/StackPanel.h"
#include "menu/Tuning.h"
#include "menu/Workspace.h"

#include "engine/Manager.h"
#include "studio/ActivePreviews.h"
#include "studio/EditResult.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/Navigation.h"
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

struct ApplicationStatus {
  ApplicationPhase phase = ApplicationPhase::kQueued;
  std::string problem;
};

std::optional<ApplicationStatus> ResolveApplication(const Frame &a_frame) {
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
    return std::nullopt;
  }
  ApplicationStatus status{latest->phase, latest->problem};
  for (const ApplicationActor &actor : latest->actors) {
    if (actor.actorID != selection.piece.actorID) {
      continue;
    }
    status.phase = actor.phase;
    status.problem = actor.problem;
    break;
  }
  return status;
}

void DrawApplicationStatus(const ApplicationStatus &a_status,
                           bool a_noOutputs) {
  if (a_status.phase == ApplicationPhase::kUnmatched && a_noOutputs) {
    Dim("Application: no outputs");
    return;
  }
  const std::string text =
      std::format("Application: {}", ApplicationPhaseName(a_status.phase));
  switch (a_status.phase) {
  case ApplicationPhase::kFailed:
    Problem(text);
    break;
  case ApplicationPhase::kUnmatched:
  case ApplicationPhase::kCancelled:
    Warn(text);
    break;
  default:
    Dim(text);
    break;
  }
  if (!a_status.problem.empty()) {
    Tooltip(a_status.problem);
  }
}

void DrawBody(const Frame &a_frame) {
  DrawPaintDraftBar(a_frame);
  if (!a_frame.recipe) {
    if (a_frame.state->paint) {
      PlaceholderText(
          "The draft destination is unavailable. Resume it after restoring the "
          "recipe, or discard it.");
    } else {
      PlaceholderText(
          "Choose a loaded recipe or create a new document above. The selected "
          "recipe may still be loading or may have been removed.");
    }
    return;
  }
  ImGui::PushID(a_frame.recipe->id.c_str());
  DrawWorkspace(a_frame);
  ImGui::PopID();
}

void ReturnToLive() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  manager->Editor().UpdateView([](Studio::View &a_live) {
    a_live.isolation = {};
    a_live.soloPiece.reset();
    a_live.muted.clear();
    a_live.freeze = false;
    a_live.speed = 1.0f;
  });
  manager->Editor().ReleaseHolds();
}

void SoloPieceButton(const Frame &a_frame) {
  const Studio::PieceRow *piece = a_frame.piece;
  if (!piece) {
    return;
  }
  const Studio::View &view = ViewOf(a_frame);
  bool solo = view.soloPiece && view.soloPiece->SamePiece(piece->ref);
  std::string text = "solo: show only this piece";
  if (solo) {
    text =
        std::format("showing only {} / {}", piece->actorName, piece->armorName);
  }
  ImGui::PushID("piece-solo");
  if (SoloButton(solo, text)) {
    Studio::Post(*a_frame.intents, Studio::SoloPiece{piece->ref, solo});
  }
  ImGui::PopID();
}

constexpr float kPieceComboWidth = 480.0f;

void DrawPieceCombo(const Frame &a_frame) {
  const Studio::PieceRow *piece = a_frame.piece;
  const std::string preview =
      piece ? Studio::PieceLabel(*piece) : std::string{"nothing applied"};
  const auto &pieces = a_frame.snapshot->pieces;
  std::vector<std::string> labels;
  std::optional<std::size_t> selected;
  labels.reserve(pieces.size());
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    const Studio::PieceRow &row = pieces[i];
    labels.push_back(Studio::PieceLabel(row));
    if (row.ref == SelectionOf(a_frame).piece) {
      selected = i;
    }
  }
  const auto picked =
      SearchCombo({.id = "##piece",
                   .preview = preview.c_str(),
                   .hint = "Search pieces",
                   .width = Studio::Width::Px(kPieceComboWidth)},
                  labels, selected);
  if (picked) {
    if (const auto *index = Get<std::size_t>(*picked)) {
      Studio::Post(*a_frame.intents, Studio::PickPiece{pieces[*index].ref});
    }
  }
}

void NavButton(const char *a_label, bool a_active,
               const std::function<void()> &a_step) {
  const float width = ButtonWidth(a_label);
  if (!a_active) {
    Disabled(true, [&] { ImGui::Button(a_label, ImVec2{width, 0.0f}); });
    return;
  }
  const auto *fill = ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_Text);
  const auto *ink = ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_WindowBg);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, *fill);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, *fill);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, *fill);
  ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, *ink);
  if (ImGui::Button(a_label, ImVec2{width, 0.0f})) {
    a_step();
  }
  ImGui::PopStyleColor(4);
}

void DrawInspectorNav(const Frame &a_frame) {
  Studio::Navigation &nav = a_frame.state->navigation;
  const bool pending = a_frame.recipe && Studio::IndexedEditPendingFor(
                                             a_frame.state->pendingIndexedEdit,
                                             a_frame.recipe->id);
  const auto step = [&](bool a_forward) {
    if (!a_frame.recipe) {
      return;
    }
    [[maybe_unused]] const bool changed =
        a_forward
            ? Studio::GoForward(nav, a_frame.state->selection, *a_frame.recipe)
            : Studio::GoBack(nav, a_frame.state->selection, *a_frame.recipe);
    if (a_frame.state->paint) {
      Studio::Reduce(*a_frame.state, Studio::SetMode{Studio::Mode::kCompose});
    }
  };
  const bool ready = a_frame.recipe && !pending;
  NavButton("<<<", ready && !nav.back.empty(), [&] { step(false); });
  ImGui::SameLine();
  RightAligned(ButtonWidth(">>>"), [&] {
    NavButton(">>>", ready && !nav.forward.empty(), [&] { step(true); });
  });
}

void DrawActivePreviews(const Frame &a_frame) {
  const std::vector<Studio::ActivePreview> previews =
      Studio::ActivePreviewsOf(ViewOf(a_frame), *a_frame.snapshot);
  const float available = ImGui::GetContentRegionAvail().x;
  float used = 0.0f;
  for (std::size_t i = 0; i < previews.size(); ++i) {
    const Studio::ActivePreview &preview = previews[i];
    const float width = TextWidth(preview.label) + ItemSpacingX() +
                        ButtonWidth("x") + ItemSpacingX();
    if (i > 0 && used + width <= available) {
      ImGui::SameLine();
      used += width;
    } else {
      used = width;
    }
    ImGui::PushID(static_cast<int>(i));
    Warn(preview.label);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      Studio::Post(*a_frame.intents, preview.clear);
    }
    ImGui::PopID();
  }
}

void DrawPieceBar(const Frame &a_frame) {
  float trailing = kPieceComboWidth;
  if (a_frame.piece) {
    trailing += ItemSpacingX() + RowButtonWidth();
  }
  const auto indicators = [&]() {
    bool shown = false;
    const auto sep = [&]() {
      if (shown) {
        ImGui::SameLine();
      }
      shown = true;
    };
    if (const auto status = ResolveApplication(a_frame)) {
      sep();
      const bool noOutputs = !Studio::MaskTaskActive(*a_frame.state) &&
                             a_frame.recipe && a_frame.recipe->outputs.empty();
      DrawApplicationStatus(*status, noOutputs);
    }
    if (Studio::IsFull(a_frame.state->previews)) {
      sep();
      DrawPreviewLimit(*a_frame.state);
    }
    if (const auto &awaited = a_frame.state->awaitedPiece) {
      sep();
      Warn(std::format("Waiting for {}: it is not loaded, not worn, or no "
                       "recipe matches it",
                       awaited->label.empty() ? "the chosen piece"
                                              : awaited->label));
    }
  };
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Piece", .leadingSpace = false}, trailing,
      [&]() {
        DrawPieceCombo(a_frame);
        if (a_frame.piece) {
          ImGui::SameLine();
          SoloPieceButton(a_frame);
        }
      },
      indicators));
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
                             Studio::kFooterTable);
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

std::string HeldSignalsText(const Frame &a_frame) {
  if (!a_frame.recipe) {
    return {};
  }
  std::string names;
  for (const Studio::SignalRow &signal : a_frame.recipe->signals) {
    if (signal.held) {
      names += (names.empty() ? "" : ", ") + signal.name;
    }
  }
  return names.empty() ? names : "Held: " + names;
}

void DrawFooter(const Frame &a_frame) {
  const Studio::View &view = ViewOf(a_frame);
  DrawInspectorNav(a_frame);
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Session", .leadingSpace = false},
      ButtonWidth("Return to live"),
      [&]() {
        Disabled(Studio::MaskTaskActive(*a_frame.state), [&] {
          if (ImGui::Button("Return to live")) {
            ReturnToLive();
          }
        });
        Tooltip("clear solo and mute, release held signals, resume the global "
                "clock at normal speed");
      },
      [&]() {
        const std::string held = HeldSignalsText(a_frame);
        if (!held.empty()) {
          Warn(held);
          ImGui::SameLine();
        }
        if (!view.freeze && view.speed == 1.0f) {
          return;
        }
        const std::string clock =
            std::format("Clock: {} at {:.2f}x",
                        view.freeze ? "held" : "running", view.speed);
        if (view.freeze) {
          Warn(clock);
        } else {
          Dim(clock);
        }
      }));
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
  if (undo && recipe->undoDepth > 0) {
    Studio::Post(out, Studio::Undo{recipe->id});
  }
  if (redo && recipe->redoDepth > 0) {
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
  const bool editPending = state.pendingIndexedEdit.has_value() ||
                           state.pendingRecipeFile.has_value() ||
                           state.pendingEditorChange.has_value() ||
                           RecipeFilePending(frame);
  DrawPieceBar(frame);
  DrawActivePreviews(frame);
  Disabled(editPending || (state.paint && state.paint->pendingCommit), [&] {
    DrawStudioContext(frame);
    DrawMaskDraftHints(frame);
  });
  const float footer =
      state.footerHeight > 0.0f
          ? state.footerHeight
          : RuleHeight() + ImGui::GetFrameHeightWithSpacing() * 2.0f;
  if (ImGui::BeginChild("studio-body", ImVec2{0.0f, -footer}, 0, 0)) {
    Disabled(editPending || (state.paint && state.paint->pendingCommit),
             [&] { DrawBody(frame); });
  }
  ImGui::EndChild();
  const float footerTop = ImGui::GetCursorPosY();
  DrawFooter(frame);
  state.footerHeight = ImGui::GetCursorPosY() - footerTop;
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
    RenderPendingStatus();
    return;
  }
  const Studio::Snapshot &snapshot = *held;
  if (snapshot.paintUpdate) {
    Studio::AcknowledgePaintUpdate(state, *snapshot.paintUpdate);
  }
  if (snapshot.paintCommit) {
    Studio::AcknowledgePaintCommit(state, *snapshot.paintCommit);
  }
  Studio::AcknowledgeEditorOperations(state, snapshot);
  Studio::ResolveEditorSelection(state, snapshot);
  BeginTuningFrame(state, snapshot);
  Studio::Intents intents;

  const Studio::PieceRow *piece =
      Studio::SelectedPiece(snapshot, state.selection);
  const Studio::RecipeRow *recipe =
      Studio::SelectedRecipe(snapshot, state.selection);
  [[maybe_unused]] const bool resolved =
      Studio::ResolveInspectorSubject(state.selection, recipe);
  [[maybe_unused]] const bool followed = Studio::ResolvePendingSubject(
      state.navigation, state.selection, state.pendingSelection, recipe);
  Studio::ReconcilePaintMode(state);
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
  DrawPreviewWindows(snapshot, state, frame.scale);
  EndTuningFrame(state);
  Dispatch(intents, state, snapshot);
  RebuildScratch(frame);
  Dispatch(intents, state, snapshot);
}

}
