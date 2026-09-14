#include "menu/Menu.h"
#include "menu/Tuning.h"

#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "menu/StudioPage.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "studio/Board.h"
#include "studio/Edits.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
Studio::FormID ActorOf(const Frame &a_frame) noexcept {
  return a_frame.piece ? a_frame.piece->ref.actorID : 0;
}

const Studio::Selection &SelectionOf(const Frame &a_frame) noexcept {
  return a_frame.state->selection;
}

const Studio::View &ViewOf(const Frame &a_frame) noexcept {
  return a_frame.snapshot->view;
}

const Studio::Layout &LayoutOf(const Frame &a_frame) noexcept {
  return a_frame.state->layout;
}

namespace {
using namespace Studio;

struct IntentPerformer {
  Manager *manager;
  MenuState &state;
  const View &view;

  void operator()(const SetMode &) const {}
  void operator()(const EditRecipe &i) const {
    const std::uint64_t request = manager->Editor().EditRecipe(
        i.recipeID, EditBatch{i.edits}, i.expectedRevision);
    if (ShouldInvalidateIndexedSubjects(i.edits)) {
      state.pendingIndexedEdit = PendingIndexedEdit{request, i.recipeID};
    }
  }
  void operator()(const SoloRecipe &i) const {
    manager->Editor().ChangeView(
        ViewCommand{Isolation::ForRecipe(i.recipeID), i.on});
  }
  void operator()(const SoloOutput &i) const {
    manager->Editor().ChangeView(
        ViewCommand{Isolation::ForOutput(i.recipeID, i.output), i.on});
  }
  void operator()(const SoloLayer &i) const {
    manager->Editor().ChangeView(
        ViewCommand{Isolation::ForLayer(i.recipeID, i.output, i.layer), i.on});
  }
  void operator()(const MuteLayer &i) const {
    manager->Editor().UpdateView([key = LayerKey{i.recipeID, i.output, i.layer},
                                  on = i.on](View &a_live) {
      if (on) {
        a_live.muted.insert(key);
      } else {
        a_live.muted.erase(key);
      }
    });
  }
  void operator()(const SetFreeze &i) const {
    manager->Editor().UpdateView([on = i.on, at = i.at](View &a_live) {
      a_live.freeze = on;
      if (on) {
        a_live.scrubSeconds = at;
      }
    });
  }
  void operator()(const SetScrub &i) const {
    manager->Editor().UpdateView([seconds = i.seconds](View &a_live) {
      a_live.freeze = true;
      a_live.scrubSeconds = seconds;
    });
  }
  void operator()(const SetSpeed &i) const {
    manager->Editor().UpdateView([speed = std::clamp(i.speed, 0.0f, 8.0f)](
                                     View &a_live) { a_live.speed = speed; });
  }
  void operator()(const StepClock &) const {
    manager->Editor().UpdateView([](View &a_live) {
      a_live.freeze = true;
      a_live.scrubSeconds +=
          static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f *
          a_live.speed;
    });
  }
  void operator()(const Undo &i) const {
    state.pendingIndexedEdit = PendingIndexedEdit{
        manager->Editor().UndoRecipe(i.recipeID), i.recipeID};
  }
  void operator()(const Redo &i) const {
    state.pendingIndexedEdit = PendingIndexedEdit{
        manager->Editor().RedoRecipe(i.recipeID), i.recipeID};
  }
  void operator()(const CreateRecipe &i) const {
    manager->Editor().NewRecipe(i.recipeID, i.key, i.geometry);
  }
  void operator()(const Studio::RenameRecipe &i) const {
    manager->Editor().RenameRecipe(i.from, i.to);
  }
  void operator()(const BeginPaint &i) const {
    manager->Editor().BeginPaint(i.recipeID, i.key, i.surface, i.sessionID,
                                 i.resetID);
  }
  void operator()(const SetPaintSurface &i) const { (void)i; }
  void operator()(const KeepPaint &i) const {
    manager->Editor().KeepPaint(i.request);
  }
  void operator()(const EndPaint &) const {
    manager->Editor().EndPaint(state.paint ? state.paint->sessionID : 0);
  }
  void operator()(const Studio::ReadMesh &i) const {
    manager->RequestMesh(i.actorID, i.geometry);
  }
  void operator()(const FireTrigger &i) const {
    manager->FireAt(i.actorID, i.event, i.node, i.offset, i.random, i.value);
  }
  void operator()(const PickPiece &) const {}
  void operator()(const PickRecipe &i) const {
    if (!i.document && view.pin && view.pin->piece == state.selection.piece &&
        view.pin->recipeID != i.recipeID) {
      manager->Editor().PinRecipe(state.selection.piece, {});
    }
  }
  void operator()(const PinRecipe &i) const {
    manager->Editor().PinRecipe(state.selection.piece, i.recipeID);
  }
  void operator()(const PickTarget &) const {}
  void operator()(const PickSlot &) const {}
  void operator()(const PickCell &) const {}
  void operator()(const PickLayer &) const {}
  void operator()(const ViewGeometry &) const {}
  void operator()(const SetStackSplit &) const {}
  void operator()(const ShowSettings &) const {}
  void operator()(const ShowResource &) const {}
  void operator()(const AddTerm &) const {}
  void operator()(const SetTermOp &) const {}
  void operator()(const SetTermText &) const {}
  void operator()(const SetTermKind &) const {}
  void operator()(const RemoveTerm &) const {}
  void operator()(const MoveTerm &) const {}
  void operator()(const PickTerm &) const {}
  void operator()(const SoloTerm &) const {}
  void operator()(const MuteTerm &) const {}
  void operator()(const LoadMask &) const {}
  void operator()(const ClearMask &) const {}
  void operator()(const UndoMask &) const {}
  void operator()(const RedoMask &) const {}
  void operator()(const ScratchRebuilt &) const {}
  void operator()(const UpdatePaint &i) const {
    manager->Editor().UpdatePaint(i.request);
  }
};
}

void Perform(const Studio::Intent &a_intent, Studio::MenuState &a_state,
             const Studio::View &a_view) {
  if (Manager *manager = Manager::GetSingleton()) {
    Match(a_intent, IntentPerformer{manager, a_state, a_view});
  }
}

namespace {
void FollowPickedSubject(const Studio::Intent &a_intent,
                         Studio::MenuState &a_state,
                         const Studio::Snapshot &a_snapshot) {
  const Studio::RecipeRow *recipe =
      Studio::SelectedRecipe(a_snapshot, a_state.selection);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(recipe, a_state.selection);
  if (!recipe) {
    return;
  }
  std::optional<Studio::InspectorSubject> subject;
  if (const auto *pick = Get<Studio::PickLayer>(a_intent)) {
    if (const auto *output =
            Get<Studio::OutputSubject>(a_state.selection.subject)) {
      subject = Studio::LayerSubject{output->output, pick->index};
    }
  } else if (const auto *pick = Get<Studio::PickCell>(a_intent);
             pick && geometry) {
    const Studio::Board board = Studio::BuildBoard(
        *recipe, *geometry, a_state.selection, a_snapshot.view);
    const auto *cell = Studio::CellAt(board, pick->surface, pick->slot);
    if (cell && cell->output) {
      subject = Studio::OutputSubject{*cell->output};
    }
  }
  if (subject) {
    [[maybe_unused]] const bool changed = Studio::Navigate(
        a_state.navigation, a_state.selection, *subject, *recipe);
  }
}
}

void Dispatch(Studio::Intents &a_intents, Studio::MenuState &a_state,
              const Studio::Snapshot &a_snapshot) {
  for (const Studio::Intent &intent : a_intents) {
    if (!Studio::AcceptIntent(a_state, intent)) {
      continue;
    }
    FinishTuning(a_state, true);
    if (Is<Studio::EditRecipe>(intent) || Is<Studio::Undo>(intent) ||
        Is<Studio::Redo>(intent)) {
      Studio::Reduce(a_state, intent);
      Perform(intent, a_state, a_snapshot.view);
    } else {
      Perform(intent, a_state, a_snapshot.view);
      Studio::Reduce(a_state, intent);
    }
    FollowPickedSubject(intent, a_state, a_snapshot);
  }
  Studio::ResolveEditorSelection(a_state, a_snapshot);
  a_intents.clear();
}

void RenderStatus(const Studio::Snapshot &a_snapshot) {
  const auto diagnostics = Trace::Get().Inspect();
  if (diagnostics.fileFailed) {
    Problem("Diagnostic trace file failed; recent events remain in memory.");
  }
  const Studio::Status &st = a_snapshot.status;
  if (st.emissivePath) {
    Ok("emissive path on");
  } else {
    Problem("emissive path OFF");
  }
  ImGui::SameLine();
  if (st.layoutVerified) {
    Ok("| layout verified");
  } else {
    Warn("| layout unverified");
  }
  ImGui::SameLine();
  if (st.textureLab) {
    Ok("| lab");
  } else {
    Warn("| no lab");
  }
  ImGui::SameLine();
  ImGui::Text("| %u actor(s), %u piece(s), %u recipe(s), %u geometr%s, %u "
              "shell(s), %u light(s), tick %u ms | %zu recipe file(s), %zu "
              "with errors",
              st.actors, st.pieces, st.recipes, st.geometries,
              st.geometries == 1 ? "y" : "ies", st.shells, st.lights,
              a_snapshot.tickMS, st.loadedFiles, st.withErrors);
}

void RenderHeader(const Studio::Snapshot &a_snapshot) {
  RenderStatus(a_snapshot);
  ImGui::Separator();
}

void RegisterMenu() {
  if (!SKSEMenuFramework::IsInstalled()) {
    logger::info("SKSE Menu Framework not installed; no in-game menu");
    return;
  }
  SKSEMenuFramework::SetSection(std::string{Identity::kMenuTitle}.c_str());
  SKSEMenuFramework::AddSectionItem("Studio", RenderStudio);
  SKSEMenuFramework::AddSectionItem("Recipes", RenderRecipes);
  SKSEMenuFramework::AddSectionItem("Setup", RenderSetup);
  logger::info("SKSE Menu Framework pages registered");
}
}
