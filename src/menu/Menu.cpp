#include "menu/Menu.h"

#include "menu/Frame.h"
#include "menu/MenuWidgets.h"
#include "menu/StudioPage.h"

#include "Identity.h"
#include "SettingsFile.h"
#include "engine/Manager.h"
#include "engine/RecipeStore.h"
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

void Perform(const Studio::Intent &a_intent, const Studio::MenuState &a_state,
             const Studio::View &a_view) {
  using namespace Studio;
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    return;
  }
  const View &view = a_view;
  Match(
      a_intent,
      [&](const SetMode &i) {
        if (a_state.paint && i.mode != Mode::kPaint &&
            a_state.mode == Mode::kPaint) {
          manager->EndPaint();
        }
      },
      [&](const EditRecipe &i) {
        manager->EditRecipe(i.recipeID, EditBatch{i.edits});
      },
      [&](const SoloRecipe &i) {
        manager->UpdateView(
            [](View &a_live) { a_live.isolatedBySolo = false; });
        manager->Isolate(i.on ? i.recipeID : std::string{}, -1, -1);
      },
      [&](const SoloOutput &i) {
        if (i.on) {
          manager->UpdateView([began = !view.Isolating()](View &a_live) {
            a_live.isolatedBySolo = a_live.isolatedBySolo || began;
          });
          manager->Isolate(i.recipeID, static_cast<int>(i.output), -1);
        } else if (view.isolatedBySolo) {
          manager->UpdateView(
              [](View &a_live) { a_live.isolatedBySolo = false; });
          manager->Isolate(std::string{}, -1, -1);
        } else {
          manager->Isolate(view.isolateRecipe, -1, -1);
        }
      },
      [&](const SoloLayer &i) {
        if (i.on) {
          manager->UpdateView([began = !view.Isolating()](View &a_live) {
            a_live.isolatedBySolo = a_live.isolatedBySolo || began;
          });
          manager->Isolate(i.recipeID, static_cast<int>(i.output),
                           static_cast<int>(i.layer));
        } else if (view.isolatedBySolo && view.isolateOutput < 0) {
          manager->UpdateView(
              [](View &a_live) { a_live.isolatedBySolo = false; });
          manager->Isolate(std::string{}, -1, -1);
        } else {
          manager->Isolate(view.isolateRecipe, view.isolateOutput, -1);
        }
      },
      [&](const MuteLayer &i) {
        manager->UpdateView([key = LayerKey{i.recipeID, i.output, i.layer},
                             on = i.on](View &a_live) {
          if (on) {
            a_live.muted.insert(key);
          } else {
            a_live.muted.erase(key);
          }
        });
      },
      [&](const SetFreeze &i) {
        manager->UpdateView([on = i.on, at = i.at](View &a_live) {
          a_live.freeze = on;
          if (on) {
            a_live.scrubSeconds = at;
          }
        });
      },
      [&](const SetScrub &i) {
        manager->UpdateView([seconds = i.seconds](View &a_live) {
          a_live.freeze = true;
          a_live.scrubSeconds = seconds;
        });
      },
      [&](const SetSpeed &i) {
        manager->UpdateView([speed = std::clamp(i.speed, 0.0f, 8.0f)](
                                View &a_live) { a_live.speed = speed; });
      },
      [&](const StepClock &) {
        manager->UpdateView([](View &a_live) {
          a_live.freeze = true;
          a_live.scrubSeconds +=
              static_cast<float>(GetSettings().TickIntervalMS()) * 0.001f *
              a_live.speed;
        });
      },
      [&](const Undo &i) { manager->UndoRecipe(i.recipeID); },
      [&](const Redo &i) { manager->RedoRecipe(i.recipeID); },
      [&](const CreateRecipe &i) {
        manager->NewRecipe(i.recipeID, i.key, i.geometry);
      },
      [&](const RenameRecipe &i) { manager->RenameRecipe(i.from, i.to); },
      [&](const BeginPaint &i) {
        manager->BeginPaint(i.recipeID, i.key, i.surface);
      },
      [&](const SetPaintSurface &i) { manager->SetPaintSurface(i.surface); },
      [&](const KeepPaint &i) { manager->KeepPaint(i.recipeID, i.name); },
      [&](const EndPaint &) { manager->EndPaint(); },
      [&](const ReadMesh &i) { manager->RequestMesh(i.actorID, i.geometry); },
      [&](const FireTrigger &i) {
        manager->FireAt(i.actorID, i.event, i.node, i.offset, i.random,
                        i.value);
      },
      [](const PickPiece &) {},
      [&](const PickRecipe &i) {
        if (view.pin && view.pin->piece == a_state.selection.piece &&
            view.pin->recipeID != i.recipeID) {
          manager->PinRecipe(a_state.selection.piece, {});
        }
      },
      [&](const PinRecipe &i) {
        manager->PinRecipe(a_state.selection.piece, i.recipeID);
      },
      [](const PickTarget &) {}, [](const PickSlot &) {},
      [](const PickCell &) {}, [](const PickLayer &) {},
      [](const ViewGeometry &) {}, [](const SetStackSplit &) {},
      [](const ShowSettings &) {}, [](const ShowResource &) {},
      [](const AddTerm &) {}, [](const SetTermOp &) {},
      [](const SetTermText &) {}, [](const SetTermKind &) {},
      [](const RemoveTerm &) {}, [](const MoveTerm &) {},
      [](const PickTerm &) {}, [](const SoloTerm &) {}, [](const MuteTerm &) {},
      [](const LoadMask &) {}, [](const ClearMask &) {},
      [](const UndoMask &) {}, [](const RedoMask &) {},
      [](const ScratchRebuilt &) {});
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

void RenderStatus(const Studio::Snapshot &) {
  Manager *manager = Manager::GetSingleton();
  if (!manager) {
    Problem("no manager");
    return;
  }
  const Manager::Status st = manager->GetStatus();
  const RecipeStoreStatus store = GetRecipeStoreStatus();
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
              st.geometries == 1 ? "y" : "ies", st.shells, st.lights, st.tickMS,
              store.loaded, store.withErrors);
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
