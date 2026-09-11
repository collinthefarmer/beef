#include "menu/Menu.h"

#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "menu/StudioPage.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "engine/Manager.h"
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
  const MenuState &state;
  const View &view;

  void operator()(const SetMode &i) const {
    if (state.paint && i.mode != Mode::kPaint && state.mode == Mode::kPaint) {
      manager->EndPaint();
    }
  }
  void operator()(const EditRecipe &i) const {
    manager->EditRecipe(i.recipeID, EditBatch{i.edits});
  }
  void operator()(const SoloRecipe &i) const {
    manager->UpdateView([](View &a_live) { a_live.isolatedBySolo = false; });
    manager->Isolate(i.on ? i.recipeID : std::string{}, -1, -1);
  }
  void operator()(const SoloOutput &i) const {
    if (i.on) {
      manager->UpdateView([began = !view.Isolating()](View &a_live) {
        a_live.isolatedBySolo = a_live.isolatedBySolo || began;
      });
      manager->Isolate(i.recipeID, static_cast<int>(i.output), -1);
    } else if (view.isolatedBySolo) {
      manager->UpdateView([](View &a_live) { a_live.isolatedBySolo = false; });
      manager->Isolate(std::string{}, -1, -1);
    } else {
      manager->Isolate(view.isolateRecipe, -1, -1);
    }
  }
  void operator()(const SoloLayer &i) const {
    if (i.on) {
      manager->UpdateView([began = !view.Isolating()](View &a_live) {
        a_live.isolatedBySolo = a_live.isolatedBySolo || began;
      });
      manager->Isolate(i.recipeID, static_cast<int>(i.output),
                       static_cast<int>(i.layer));
    } else if (view.isolatedBySolo && view.isolateOutput < 0) {
      manager->UpdateView([](View &a_live) { a_live.isolatedBySolo = false; });
      manager->Isolate(std::string{}, -1, -1);
    } else {
      manager->Isolate(view.isolateRecipe, view.isolateOutput, -1);
    }
  }
  void operator()(const MuteLayer &i) const {
    manager->UpdateView([key = LayerKey{i.recipeID, i.output, i.layer},
                         on = i.on](View &a_live) {
      if (on) {
        a_live.muted.insert(key);
      } else {
        a_live.muted.erase(key);
      }
    });
  }
  void operator()(const SetFreeze &i) const {
    manager->UpdateView([on = i.on, at = i.at](View &a_live) {
      a_live.freeze = on;
      if (on) {
        a_live.scrubSeconds = at;
      }
    });
  }
  void operator()(const SetScrub &i) const {
    manager->UpdateView([seconds = i.seconds](View &a_live) {
      a_live.freeze = true;
      a_live.scrubSeconds = seconds;
    });
  }
  void operator()(const SetSpeed &i) const {
    manager->UpdateView([speed = std::clamp(i.speed, 0.0f, 8.0f)](
                            View &a_live) { a_live.speed = speed; });
  }
  void operator()(const StepClock &) const {
    manager->UpdateView([](View &a_live) {
      a_live.freeze = true;
      a_live.scrubSeconds +=
          static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f *
          a_live.speed;
    });
  }
  void operator()(const Undo &i) const { manager->UndoRecipe(i.recipeID); }
  void operator()(const Redo &i) const { manager->RedoRecipe(i.recipeID); }
  void operator()(const CreateRecipe &i) const {
    manager->NewRecipe(i.recipeID, i.key, i.geometry);
  }
  void operator()(const Studio::RenameRecipe &i) const {
    manager->RenameRecipe(i.from, i.to);
  }
  void operator()(const BeginPaint &i) const {
    manager->BeginPaint(i.recipeID, i.key, i.surface);
  }
  void operator()(const SetPaintSurface &i) const {
    manager->SetPaintSurface(i.surface);
  }
  void operator()(const KeepPaint &i) const {
    manager->KeepPaint(i.recipeID, i.name);
  }
  void operator()(const EndPaint &) const { manager->EndPaint(); }
  void operator()(const Studio::ReadMesh &i) const {
    manager->RequestMesh(i.actorID, i.geometry);
  }
  void operator()(const FireTrigger &i) const {
    manager->FireAt(i.actorID, i.event, i.node, i.offset, i.random, i.value);
  }
  void operator()(const PickPiece &) const {}
  void operator()(const PickRecipe &i) const {
    if (view.pin && view.pin->piece == state.selection.piece &&
        view.pin->recipeID != i.recipeID) {
      manager->PinRecipe(state.selection.piece, {});
    }
  }
  void operator()(const PinRecipe &i) const {
    manager->PinRecipe(state.selection.piece, i.recipeID);
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
};
}

void Perform(const Studio::Intent &a_intent, const Studio::MenuState &a_state,
             const Studio::View &a_view) {
  if (Manager *manager = Manager::GetSingleton()) {
    Match(a_intent, IntentPerformer{manager, a_state, a_view});
  }
}

void Dispatch(Studio::Intents &a_intents, Studio::MenuState &a_state,
              const Studio::Snapshot &a_snapshot) {
  for (const Studio::Intent &intent : a_intents) {
    Perform(intent, a_state, a_snapshot.view);
    Studio::Reduce(a_state, intent);
  }
  Studio::ResolveSelection(a_state.selection, a_snapshot);
  a_intents.clear();
}

void RenderStatus(const Studio::Snapshot &a_snapshot) {
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
  if (st.runtimeLab) {
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
