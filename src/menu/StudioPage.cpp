#include "menu/StudioPage.h"

#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/Frame.h"
#include "menu/Menu.h"
#include "menu/MenuWidgets.h"
#include "menu/PaintPanel.h"
#include "menu/ResourcePanels.h"
#include "menu/StackPanel.h"

#include "engine/Manager.h"
#include "studio/Board.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
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
constexpr std::size_t kSettingsColumns = 2;
constexpr Studio::TableStyle kFooterStyle{.borders = Studio::TableBorders::kAll,
                                          .stretch = true,
                                          .headers = true,
                                          .rowBackground = false};

void DrawBody(const Frame &a_frame) {
  const Studio::PieceRow *piece = a_frame.piece;
  const Studio::RecipeRow *recipe = a_frame.recipe;
  Studio::MenuState &state = *a_frame.state;
  Studio::Intents &out = *a_frame.intents;

  if (!piece || !recipe) {
    Dim(state.paint ? "starting the paint recipe"
                    : "nothing applied; equip enchanted PBR armor or press "
                      "Re-apply all on the Recipes page");
    return;
  }

  const Studio::GeometryRow *geometry = a_frame.geometry;
  if (!geometry) {
    ImGui::PushID(recipe->id.c_str());
    DrawPaintHead(a_frame);
    Rule();
    Dim(std::format("recipe {} is bound to no geometry of this piece: its keys "
                    "or selectors match none of its geometries",
                    recipe->id));
    ImGui::PopID();
    return;
  }

  const Studio::View &view = ViewOf(a_frame);
  const Studio::Selection &selection = SelectionOf(a_frame);
  const Studio::Layout &layout = LayoutOf(a_frame);
  ImGui::PushID(recipe->id.c_str());

  const Studio::Board board =
      Studio::BuildBoard(*recipe, *geometry, selection, view);
  const PaneChoice pane = ChoosePane(selection.target, state.settings);
  const Studio::Cell *picked =
      layout.contextRows ? DrawContext(board, a_frame) : nullptr;
  const bool painting = layout.maskEditor;
  const bool painterReady =
      painting && state.paint && recipe->id == Studio::kPaintRecipe;
  if (!layout.contextRows) {
    DrawPaintHead(a_frame);
    Rule();
    if (!state.paint) {
      const Studio::RecipeRow *active = recipe;
      if (view.Isolating() && view.isolateRecipe != Studio::kPaintRecipe) {
        const auto it = std::ranges::find(piece->recipes, view.isolateRecipe,
                                          &Studio::RecipeRow::id);
        if (it != piece->recipes.end() && it->id != recipe->id) {
          active = &*it;
          Studio::Post(out, Studio::PickRecipe{it->id});
        }
      }
      if (const std::optional<RecipeKey> key = DefaultKeyOf(*piece)) {
        Studio::Post(out,
                     Studio::BeginPaint{active->id, *key, Surface::kMaterial});
      } else {
        Warn("the piece offers no key to paint on");
      }
    } else if (painterReady) {
      for (const Studio::GeometryRow &g : recipe->geometries) {
        if (!state.paint->readGeometries.contains(g.name)) {
          Studio::Post(out, Studio::ReadMesh{piece->ref.actorID, g.name});
        }
      }
    } else {
      Dim("starting the paint recipe");
    }
  }
  const bool resources = layout.signals;
  if (painting) {
    const std::string title = state.mask.editing.empty()
                                  ? std::string{"Mask"}
                                  : std::format("Mask: {}", state.mask.editing);
    DrawMaskRule(title, a_frame);
  } else {
    const std::string_view title =
        pane.settings ? (selection.target == Target::kLight ? "Light settings"
                                                            : "Shell settings")
                      : "Stack";
    DrawPaneRule(title, pane, board, a_frame);
  }
  const float under = ImGui::GetContentRegionAvail().y;
  const float resourcesHeight =
      resources ? under * layout.resourcesShare : 0.0f;
  const float stackHeight =
      resources ? -(resourcesHeight + RuleHeight()) : 0.0f;

  if (ImGui::BeginChild("stack-pane", ImVec2{0.0f, stackHeight}, 0, 0)) {
    if (painting) {
      if (painterReady) {
        DrawMaskStack(a_frame);
      }
    } else if (pane.settings) {
      if (selection.target == Target::kLight) {
        if (recipe->lightRow.present) {
          DrawFormWithSignals("light",
                              Studio::LightForm(recipe->lightRow,
                                                Studio::SignalNamesOf(*recipe)),
                              a_frame, kSettingsColumns);
        } else {
          Dim("the recipe has no light");
        }
      } else {
        DrawFormWithSignals(
            "shell",
            Studio::ShellForm(recipe->shellRow, Studio::SignalNamesOf(*recipe)),
            a_frame, kSettingsColumns);
      }
    } else if (picked && picked->output) {
      const std::optional<Studio::LayerStack> stack = Studio::BuildStackView(
          Studio::StackViewInput{*piece, *recipe, *geometry, selection, view});
      const std::optional<Studio::Inspector> inspector =
          layout.inspector
              ? Studio::BuildInspector(*recipe, *geometry, selection)
              : std::nullopt;
      DrawStack(stack, inspector, a_frame);
    }
  }
  ImGui::EndChild();

  if (resources) {
    const std::string_view filter = DrawResourcesRule(a_frame);
    DrawResources(a_frame, filter);
  }
  ImGui::PopID();
}

void DrawFooter(const Frame &a_frame) {
  const Studio::RecipeRow *recipe = a_frame.recipe;
  const Studio::View &view = ViewOf(a_frame);
  Studio::Intents &out = *a_frame.intents;
  const float now = recipe ? recipe->time : 0.0f;
  Rule(Studio::RuleSpec{.text = "Timeline"});
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

void HistoryKeys(const Frame &a_frame) {
  const Studio::RecipeRow *recipe = a_frame.recipe;
  const Studio::MenuState &state = *a_frame.state;
  const auto *io = ImGui::GetIO();
  if (!recipe || !io || !io->KeyCtrl || state.activeField != Studio::kNoField) {
    return;
  }
  const bool undo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Z, false);
  const bool redo = ImGui::IsKeyPressed(ImGuiMCP::ImGuiKey_Y, false);
  Studio::Intents &out = *a_frame.intents;
  if (state.paint) {
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
}

void __stdcall RenderStudio() {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  Studio::MenuState &state = Studio::State();
  manager->Watch(Studio::RequestOf(state.selection));
  const std::shared_ptr<const Manager::Snapshot> held =
      manager->LatestSnapshot();
  if (!held) {
    return;
  }
  const Studio::Snapshot &snapshot = *held;
  Studio::ResolveSelection(state.selection, snapshot);
  Studio::Intents intents;

  Studio::Mode mode = state.mode;
  if (ModeBar(mode, state.modeDrawn)) {
    Studio::Post(intents, Studio::SetMode{mode});
  }

  const Studio::PieceRow *piece =
      Studio::SelectedPiece(snapshot, state.selection);
  const Studio::RecipeRow *recipe =
      Studio::SelectedRecipe(piece, state.selection);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(recipe, state.selection);
  const Studio::Names names = (recipe && geometry)
                                  ? Studio::NamesOf(*recipe, *geometry)
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

  const float footer =
      RuleHeight() + ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f;
  if (ImGui::BeginChild("studio-body", ImVec2{0.0f, -footer}, 0, 0)) {
    DrawBody(frame);
  }
  ImGui::EndChild();
  DrawFooter(frame);
  HistoryKeys(frame);
  Dispatch(intents, state, snapshot);
  RebuildScratch(frame);
}
}
