#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/EngineForms.h"
#include "engine/ManagerShared.h"
#include "engine/RecipeStore.h"
#include "render/Binding.h"
#include "render/Compositor.h"
#include "render/RuntimeTextures.h"
#include "studio/Edits.h"
#include "studio/Panels.h"
#include "studio/RecipeContent.h"
#include "studio/Rows.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::vector<Studio::SlotRow> SlotRows(const SlotTarget &a_target) {
  std::vector<Studio::SlotRow> rows;
  for (const SlotState &s : a_target.Slots()) {
    rows.push_back({s.slot, s.original, s.written, a_target.Problem(s.slot)});
  }
  return rows;
}
}

void Manager::Isolate(std::string a_recipe, int a_output, int a_layer) {
  PostTask([this, recipe = std::move(a_recipe), a_output, a_layer] {
    const bool changed = view_.isolateRecipe != recipe;
    view_.isolateRecipe = recipe;
    view_.isolateOutput = a_output;
    view_.isolateLayer = a_layer;
    if (changed) {
      ReapplyAll();
    }
  });
}

void Manager::PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID) {
  PostTask([this, a_piece, id = std::move(a_recipeID)] {
    std::optional<Studio::Pin> pin;
    if (!id.empty()) {
      const std::span<const Recipe> loaded = LoadedRecipes();
      if (std::ranges::find(loaded, id, &Recipe::id) == loaded.end()) {
        logger::warn("pin: recipe {} is not loaded", id);
        return;
      }
      pin = Studio::Pin{a_piece, id};
    }
    if (view_.pin == pin) {
      return;
    }
    WithListMoved([&] { view_.pin = pin; });
    if (pin) {
      logger::info("pin: {} shown on armor {:08X} of actor {:08X} ({}) while "
                   "viewed",
                   id, a_piece.armorID, a_piece.actorID,
                   a_piece.firstPerson ? "1st" : "3rd");
    } else {
      logger::info("pin: cleared");
    }
  });
}

void Manager::UpdateView(std::function<void(Studio::View &)> a_change) {
  PostTask([this, change = std::move(a_change)] { change(view_); });
}

Manager::Status Manager::GetStatus() const {
  Status s;
  s.emissivePath = emissivePathEnabled_;
  s.layoutVerified = layoutVerified_;
  s.runtimeLab = TextureLab::GetSingleton()->Available();
  s.actors = static_cast<std::uint32_t>(applied_.size());
  for (const auto &[id, state] : applied_) {
    s.pieces += static_cast<std::uint32_t>(state.pieces.size());
    s.recipes += static_cast<std::uint32_t>(state.instances.size());
    for (const LivePiece &piece : state.pieces) {
      for (const LiveGeometry &bound : piece.geometries) {
        if (bound.lost) {
          continue;
        }
        ++s.geometries;
        s.shells += bound.shell ? 1 : 0;
      }
    }
    for (const LiveInstance &instance : state.instances) {
      s.lights += instance.light ? 1 : 0;
    }
  }
  s.tickMS = GetSettings().TickIntervalMS();
  return s;
}

void Manager::Watch(const std::optional<Studio::PieceRef> &a_request) {
  std::scoped_lock lock{snapshotLock_};
  watch_ = a_request;
  watchedMS_ = NowMS();
}

std::shared_ptr<const Manager::Snapshot> Manager::LatestSnapshot() const {
  std::scoped_lock lock{snapshotLock_};
  return latest_;
}

void Manager::PublishSnapshot(std::uint32_t a_nowMS) {
  std::optional<Studio::PieceRef> request;
  {
    std::scoped_lock lock{snapshotLock_};
    if (watchedMS_ == 0 || a_nowMS > watchedMS_ + kWatchWindowMS) {
      return;
    }
    request = watch_;
  }
  auto built = std::make_shared<Snapshot>(BuildSnapshot(request));
  built->version = ++snapshotVersion_;
  built->view = view_;
  std::scoped_lock lock{snapshotLock_};
  latest_ = std::move(built);
}

Manager::Snapshot
Manager::BuildSnapshot(const std::optional<Studio::PieceRef> &a_request) const {
  Snapshot out;
  const Status status = GetStatus();
  const RecipeStoreStatus store = GetRecipeStoreStatus();
  out.tickMS = status.tickMS;
  out.status = {status.emissivePath, status.layoutVerified, status.runtimeLab,
                status.actors,       status.pieces,         status.recipes,
                status.geometries,   status.shells,         status.lights,
                store.loaded,        store.withErrors};
  Compositor *compositor = Compositor::GetSingleton();

  bool anyMatch = false;
  for (const auto &[actorID, state] : applied_) {
    std::size_t flatBase = 0;
    for (const LivePiece &piece : state.pieces) {
      const bool firstPerson = flatBase < state.structure.pieces.size() &&
                               state.structure.pieces[flatBase].firstPerson;
      const Studio::PieceRef ref{actorID, piece.armor, firstPerson};
      if (a_request && *a_request == ref) {
        anyMatch = true;
      }
      flatBase += piece.geometries.size();
    }
  }

  bool first = true;
  for (const auto &[actorID, state] : applied_) {
    const RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
    std::size_t flatBase = 0;
    for (const LivePiece &piece : state.pieces) {
      const std::size_t flatStart = flatBase;
      const std::size_t geomCount = piece.geometries.size();
      flatBase += geomCount;

      const std::vector<PieceMatch> matches =
          MatchesForPiece(state.structure, flatStart, geomCount);
      if (matches.empty()) {
        continue;
      }
      const bool firstPerson = flatStart < state.structure.pieces.size() &&
                               state.structure.pieces[flatStart].firstPerson;
      Studio::PieceRow row;
      row.ref = Studio::PieceRef{actorID, piece.armor, firstPerson};
      row.actorName = actor && actor->GetName() ? actor->GetName() : "?";
      row.armorName = piece.armorName;
      const bool full = anyMatch ? (a_request && *a_request == row.ref) : first;
      first = false;

      if (flatStart < state.structure.pieces.size()) {
        for (const PieceKey &source :
             KeyChoicesOf(state.structure.pieces[flatStart].keys)) {
          Studio::KeyChoice key;
          key.key = source;
          const RE::TESForm *form = LookupForm(source.form);
          const std::string editorID = form ? EditorIdOf(*form) : std::string{};
          key.text = editorID.empty() ? source.form.ToString() : editorID;
          row.keys.push_back(std::move(key));
        }
      }

      for (const PieceMatch &match : matches) {
        if (match.instance >= state.instances.size()) {
          continue;
        }
        const LiveInstance &instance = state.instances[match.instance];
        if (!instance.recipe) {
          continue;
        }
        const Recipe &recipe = *instance.recipe;
        std::size_t undoDepth = 0;
        std::size_t redoDepth = 0;
        if (const auto history = histories_.find(recipe.id);
            history != histories_.end()) {
          undoDepth = history->second.UndoDepth();
          redoDepth = history->second.RedoDepth();
        }
        Studio::ReferenceCounts references;
        std::vector<Diagnostic> problems;
        if (full) {
          const Studio::ReferenceCounts *counted = ReferencesOf(recipe.id);
          references = counted ? *counted : Studio::CountReferences(recipe);
          if (const std::optional<RecipeOrigin> origin = OriginOf(recipe)) {
            problems.assign(origin->diagnostics.begin(),
                            origin->diagnostics.end());
          }
        }
        const bool pinned = view_.pin && view_.pin->piece == row.ref &&
                            view_.pin->recipeID == recipe.id;
        Studio::RecipeRow r = Studio::RecipeContent(
            {recipe, match.key, match.priority, instance.lastTime,
             instance.lightOutput, IsDirty(recipe.id), pinned, full, undoDepth,
             redoDepth, references, instance.graph.get(),
             instance.signals.get(), problems});
        if (!full) {
          row.recipes.push_back(std::move(r));
          continue;
        }

        for (const LiveGeometry &bound : piece.geometries) {
          if (bound.lost) {
            continue;
          }
          const std::optional<std::size_t> placedIndex = PlacedIndexOf(
              state.structure, bound.placements, InstanceId{match.instance});
          if (!placedIndex) {
            continue;
          }
          const std::size_t pid =
              static_cast<std::size_t>(bound.placements[*placedIndex]);
          if (pid >= state.placements.size()) {
            continue;
          }
          const LivePlacement &placement = state.placements[pid];
          Studio::GeometryRow gr;
          gr.name = bound.name;
          gr.privateMaterial = bound.material && bound.material->Private();
          gr.shell = bound.shell ? bound.shell->Describe() : "";
          if (const std::shared_ptr<const MeshEntry> entry =
                  compositor->CachedMesh(bound.geometry.get());
              entry && entry->mesh) {
            gr.meshRead = true;
            gr.partitions = entry->facts.slots;
            gr.bones = entry->facts.bones;
            gr.islands = entry->analysis.islands;
          }
          if (const Compositor::MaterialRecord *material =
                  compositor->CachedMaterial(bound.inputs.material);
              material && material->analysis) {
            gr.clusters = material->analysis->clusters;
          }
          gr.materialSlots = bound.material ? SlotRows(*bound.material)
                                            : std::vector<Studio::SlotRow>{};
          gr.shellSlots = bound.shell ? SlotRows(*bound.shell)
                                      : std::vector<Studio::SlotRow>{};
          for (const Source &source : recipe.sources) {
            Studio::PictureRow prow;
            prow.name = source.name;
            prow.description = DescribeSource(source.kind);
            prow.type = SourceType(source);
            if (const std::optional<PreparedSource> prepared =
                    compositor->InspectSource(recipe, source.name,
                                              bound.inputs)) {
              prow.texture = prepared->texture.get();
              prow.channel = prepared->sampling.channel;
              prow.animated = prepared->animated;
              prow.problem = prepared->problem;
            }
            gr.sources.push_back(std::move(prow));
          }
          for (const Mask &mask : recipe.masks) {
            Studio::PictureRow prow;
            prow.name = mask.name;
            prow.description = mask.text;
            if (const std::optional<PreparedMask> prepared =
                    compositor->InspectMask(recipe, mask.name, bound.inputs)) {
              prow.texture = prepared->texture.get();
              prow.channel = prepared->channel;
              prow.animated = prepared->animated;
              prow.problem = prepared->problem;
            }
            gr.masks.push_back(std::move(prow));
          }
          for (const PlacedOutput &o : placement.outputs) {
            Studio::OutputRow orow = Studio::OutputRowOf(recipe, o.index);
            orow.animated = o.stack && o.stack->Animated();
            orow.size = o.stack ? o.stack->Size().Pixels() : 0;
            orow.problem = o.problem;
            orow.texture = o.stack ? o.stack->Texture() : nullptr;
            if (const std::optional<std::size_t> merge = ChainIndexOf(
                    bound.plan,
                    SlotContribution{SlotSource{*placedIndex}, o.index})) {
              orow.merged = true;
              orow.merge = *merge;
            }
            const SurfaceOutput *material =
                o.index < recipe.outputs.size()
                    ? Get<SurfaceOutput>(recipe.outputs[o.index])
                    : nullptr;
            if (material && instance.signals) {
              const SlotScalars &sc = material->scalars;
              const SignalState &sig = *instance.signals;
              std::size_t si = 0;
              for (const ScalarField field : ScalarsOf(material->slot)) {
                if (field == ScalarField::kColor) {
                  if (!sc.color) {
                    continue;
                  }
                  if (si < orow.scalars.size()) {
                    orow.scalars[si].value = sig.Resolve(*sc.color);
                  }
                } else {
                  const std::optional<Param> *param = ScalarOf(sc, field);
                  if (!param || !*param) {
                    continue;
                  }
                  if (si < orow.scalars.size()) {
                    orow.scalars[si].value = sig.Resolve(**param);
                  }
                }
                ++si;
              }
              for (std::size_t i = 0;
                   i < material->stack.size() && i < orow.layers.size(); ++i) {
                orow.layers[i].opacity =
                    sig.Resolve(material->stack[i].opacity);
              }
              if (o.stack) {
                for (const PreparedLayer &prepared : o.stack->Layers()) {
                  if (prepared.index < orow.layers.size() && prepared.source) {
                    orow.layers[prepared.index].texture =
                        prepared.source->texture.get();
                  }
                }
                for (const Diagnostic &d : o.stack->Diagnostics()) {
                  if (d.where.starts_with("layer ")) {
                    const unsigned long at =
                        std::strtoul(d.where.c_str() + 6, nullptr, 10);
                    if (at < orow.layers.size() &&
                        orow.layers[at].problem.empty()) {
                      orow.layers[at].problem = d.message;
                    }
                  }
                }
              }
            }
            gr.outputs.push_back(std::move(orow));
          }
          r.geometries.push_back(std::move(gr));
        }
        r.light = instance.light ? instance.light->Describe() : "";
        row.recipes.push_back(std::move(r));
      }
      out.pieces.push_back(std::move(row));
    }
  }
  for (const Recipe &recipe : LoadedRecipes()) {
    if (IsTransient(recipe.id)) {
      continue;
    }
    Studio::LoadedRecipeRow row;
    row.id = recipe.id;
    row.keys = recipe.keys;
    row.signals = recipe.signals.size();
    row.curves = recipe.curves.size();
    row.sources = recipe.sources.size();
    row.masks = recipe.masks.size();
    row.outputs = recipe.outputs.size();
    row.imported = !recipe.metadata.imported.empty();
    if (const std::optional<RecipeOrigin> origin = OriginOf(recipe)) {
      row.diagnostics.assign(origin->diagnostics.begin(),
                             origin->diagnostics.end());
      row.path = origin->path.string();
    }
    out.loaded.push_back(recipe.id);
    out.loadedRecipes.push_back(std::move(row));
  }
  return out;
}
}
