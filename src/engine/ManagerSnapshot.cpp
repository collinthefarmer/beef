// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/InputCatalog.h"
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/Clock.h"
#include "engine/EngineForms.h"
#include "engine/GameObjectService.h"
#include "engine/RecipeStore.h"
#include "engine/Tweaks.h"
#include "render/Binding.h"
#include "render/Compositor.h"
#include "render/SourceSampling.h"
#include "render/TextureLab.h"
#include "studio/Edits.h"
#include "studio/Panels.h"
#include "studio/RecipeSnapshot.h"
#include "studio/Rows.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
void PublishCatalogs(Studio::Snapshot &a_out, std::uint32_t a_actor) {
  a_out.catalogEventActor = a_actor;
  for (std::size_t kind = 0; kind < Studio::kGameObjectKindCount; ++kind) {
    const auto value = static_cast<Studio::GameObjectKind>(kind);
    a_out.catalogs[kind] = value == Studio::GameObjectKind::kAnimEvent
                               ? AnimEventCatalogOf(a_actor)
                               : GameObjectCatalogOf(value);
  }
  a_out.actorValueSamples =
      BuildActorValueSamples(*a_out.catalogs[static_cast<std::size_t>(
                                 Studio::GameObjectKind::kActorValue)],
                             a_actor);
}

Studio::TextureHandle TextureHandleOf(RE::NiSourceTexture *a_texture) {
  return static_cast<Studio::TextureHandle>(
      reinterpret_cast<std::uintptr_t>(a_texture));
}

Studio::TextureHandle RetainTexture(Manager::Snapshot &a_snapshot,
                                    const TextureRef &a_texture) {
  if (a_texture) {
    a_snapshot.textures.emplace_back(a_texture);
    return TextureHandleOf(a_snapshot.textures.back().get());
  }
  return Studio::TextureHandle{};
}

std::vector<Studio::SlotRow> SlotRows(const SlotTarget &a_target) {
  std::vector<Studio::SlotRow> rows;
  for (const SlotState &s : a_target.Slots()) {
    rows.push_back(
        {s.slot, s.original, s.written, ProblemText(a_target.Problem(s.slot))});
  }
  return rows;
}
struct GeometrySnapshotBuilder {
  Manager::Snapshot &snapshot;
  const Recipe &recipe;
  const RecipeGraph &graph;
  std::size_t applicationContext;
  const SignalState *signals;
  const LiveGeometry &bound;
  std::size_t placedIndex;

  void InspectSources(Studio::GeometryRow &row) const {
    auto *compositor = Compositor::GetSingleton();
    GeometryInputs inputs = bound.inputs;
    inputs.applicationContext = applicationContext;
    for (const Source &source : recipe.sources) {
      Studio::PictureRow picture;
      picture.name = source.name;
      picture.description = DescribeSource(source.kind);
      picture.type = SourceType(source);
      if (const std::optional<PreparedSource> prepared =
              compositor->InspectSource(recipe, graph, source.name,
                                        bound.inputs)) {
        picture.channel = prepared->sampling.channel;
        picture.animated = prepared->animated;
        picture.problem = prepared->problem;
        if (Is<ImageSource>(source.kind) && signals && prepared->texture) {
          const std::string context = std::format(
              "{}:{}:{}", recipe.id,
              reinterpret_cast<std::uintptr_t>(bound.geometry.get()),
              source.name);
          auto *lab = TextureLab::GetSingleton();
          const auto preview =
              lab ? lab->SampledPreview(
                        context, prepared->texture.get(),
                        TextureLab::PreviewSampling{prepared->sampling,
                                                    prepared->normalize},
                        prepared->animated)
                  : nullptr;
          picture.texture = RetainTexture(snapshot, TextureRef{preview});
          picture.channel = ShaderChannel::kRgb;
        } else {
          picture.texture = RetainTexture(snapshot, prepared->texture);
        }
      }
      row.sources.push_back(std::move(picture));
    }
  }

  void InspectMasks(Studio::GeometryRow &row) const {
    auto *compositor = Compositor::GetSingleton();
    GeometryInputs inputs = bound.inputs;
    inputs.applicationContext = applicationContext;
    for (const Mask &mask : recipe.masks) {
      Studio::PictureRow picture;
      picture.name = mask.name;
      picture.description = mask.text;
      if (const std::optional<PreparedMask> prepared =
              compositor->InspectMask(recipe, graph, mask.name, inputs)) {
        picture.texture = RetainTexture(snapshot, prepared->texture);
        picture.channel = prepared->channel;
        picture.animated = prepared->animated;
        picture.problem = prepared->problem;
      }
      row.masks.push_back(std::move(picture));
    }
  }

  void ResolveScalars(Studio::OutputRow &row,
                      const SurfaceOutput &material) const {
    const SlotScalars &scalars = material.scalars;
    std::size_t scalarIndex = 0;
    for (const ScalarField field : ScalarsOf(material.slot)) {
      if (field == ScalarField::kColor) {
        if (!scalars.color) {
          continue;
        }
        if (scalarIndex < row.scalars.size()) {
          row.scalars[scalarIndex].value = signals->Resolve(*scalars.color);
        }
      } else {
        const std::optional<Param> *param = ScalarOf(scalars, field);
        if (!param || !*param) {
          continue;
        }
        if (scalarIndex < row.scalars.size()) {
          row.scalars[scalarIndex].value = signals->Resolve(**param);
        }
      }
      ++scalarIndex;
    }
  }

  void InspectLayers(Studio::OutputRow &row, const SurfaceOutput &material,
                     const PlacedOutput &output) const {
    for (std::size_t i = 0; i < material.stack.size() && i < row.layers.size();
         ++i) {
      row.layers[i].opacity = signals->Resolve(material.stack[i].opacity);
    }
    if (!output.stack) {
      return;
    }
    for (std::size_t i = 0; i < row.layers.size(); ++i)
      row.layers[i].texture =
          RetainTexture(snapshot, output.stack->LayerTexture(i));
    for (const Diagnostic &diagnostic : output.stack->Diagnostics()) {
      if (diagnostic.where.starts_with("layer ")) {
        const unsigned long at =
            std::strtoul(diagnostic.where.c_str() + 6, nullptr, 10);
        if (at < row.layers.size() && row.layers[at].problem.empty()) {
          row.layers[at].problem = diagnostic.message;
        }
      }
    }
  }

  [[nodiscard]] Studio::OutputRow
  BuildOutput(const PlacedOutput &output) const {
    Studio::OutputRow row = Studio::OutputRowOf(recipe, output.index);
    row.animated = output.stack && output.stack->Animated();
    row.size = output.stack ? output.stack->Size().Pixels() : 0;
    row.problem = output.problem;
    row.texture = RetainTexture(snapshot, output.stack ? output.stack->Texture()
                                                       : nullptr);
    if (const std::optional<std::size_t> merge = ChainIndexOf(
            bound.plan,
            SlotContribution{SlotContributor{placedIndex}, output.index})) {
      row.merged = true;
      row.merge = *merge;
    }
    const SurfaceOutput *material = SurfaceOutputOf(recipe, output.index);
    if (material && signals) {
      ResolveScalars(row, *material);
      InspectLayers(row, *material, output);
    }
    return row;
  }

  [[nodiscard]] Studio::GeometryRow
  Build(const LivePlacement &placement) const {
    auto *compositor = Compositor::GetSingleton();
    Studio::GeometryRow row;
    row.name = bound.name;
    row.privateMaterial = bound.material && bound.material->Private();
    row.shell = bound.shell ? bound.shell->Describe() : "";
    if (const std::shared_ptr<const MeshEntry> entry =
            compositor->CachedMesh(bound.geometry.get());
        entry && entry->mesh) {
      row.meshRead = true;
      row.partitions = entry->facts.slots;
      row.bones = entry->facts.bones;
      row.islands = entry->analysis.islands;
    }
    if (const Compositor::MaterialRecord *material =
            compositor->CachedMaterial(bound.inputs.material);
        material && material->analysis) {
      row.clusters = material->analysis->clusters;
    }
    row.materialSlots = bound.material ? SlotRows(*bound.material)
                                       : std::vector<Studio::SlotRow>{};
    row.shellSlots =
        bound.shell ? SlotRows(*bound.shell) : std::vector<Studio::SlotRow>{};
    InspectSources(row);
    InspectMasks(row);
    for (const PlacedOutput &output : placement.outputs) {
      row.outputs.push_back(BuildOutput(output));
    }
    return row;
  }
};

struct PieceSnapshotBuilder {
  Manager::Snapshot &snapshot;
  const RecipeEditor &editor;
  const LiveActor &state;
  const LivePiece &piece;
  Studio::PieceRef ref;
  std::size_t flatStart = 0;
  bool full = false;

  void InspectGeometries(Studio::RecipeRow &row, const LiveInstance &instance,
                         std::size_t instanceIndex) const {
    for (const LiveGeometry &bound : piece.geometries) {
      if (bound.lost) {
        continue;
      }
      const std::optional<std::size_t> placedIndex = PlacedIndexOf(
          state.plan, bound.placements, InstanceId{instanceIndex});
      if (!placedIndex) {
        continue;
      }
      const std::optional<ResolvedPlacement> resolved =
          ResolvePlacement(state, bound, *placedIndex);
      if (!resolved || !instance.graph) {
        continue;
      }
      const LivePlacement &placement = state.placements[resolved->placement];
      row.geometries.push_back(GeometrySnapshotBuilder{
          snapshot, *instance.recipe, *instance.graph, instanceIndex + 1,
          instance.signals.get(), bound, *placedIndex}
                                   .Build(placement));
    }
  }

  [[nodiscard]] std::optional<Studio::RecipeRow>
  BuildRecipe(const PieceMatch &match) const {
    const auto &view = editor.CurrentView();
    if (match.instance >= state.instances.size()) {
      return std::nullopt;
    }
    const LiveInstance &instance = state.instances[match.instance];
    if (!instance.recipe) {
      return std::nullopt;
    }
    const Recipe &recipe = *instance.recipe;
    std::size_t undoDepth = 0;
    std::size_t redoDepth = 0;
    if (const Studio::History<Recipe> *history = editor.HistoryOf(recipe.id)) {
      undoDepth = history->UndoDepth();
      redoDepth = history->RedoDepth();
    }
    Studio::ReferenceCounts references;
    std::vector<Diagnostic> problems;
    if (full) {
      const Studio::ReferenceCounts *counted = ReferencesOf(recipe.id);
      references = counted ? *counted : Studio::CountReferences(recipe);
      if (const std::optional<RecipeOrigin> origin = OriginOf(recipe)) {
        problems.assign(origin->diagnostics.begin(), origin->diagnostics.end());
      }
    }
    const bool pinned =
        view.pin && view.pin->piece == ref && view.pin->recipeID == recipe.id;
    Studio::RecipeRow row = Studio::BuildRecipeRow(
        {recipe, match.key, match.priority, instance.lastTime,
         instance.lightOutput, IsDirty(recipe.id), pinned, full, undoDepth,
         redoDepth, references, instance.graph.get(), instance.signals.get(),
         problems});
    row.documentRevision = editor.DocumentRevisionOf(recipe.id);
    if (!full) {
      return row;
    }

    InspectGeometries(row, instance, match.instance);
    row.light =
        instance.light ? instance.light->Describe() : instance.lightProblem;
    return row;
  }

  [[nodiscard]] Studio::PieceRow
  Build(const std::vector<PieceMatch> &matches) const {
    const RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(ref.actorID);
    Studio::PieceRow row;
    row.ref = ref;
    row.isPlayer =
        actor != nullptr && actor == RE::PlayerCharacter::GetSingleton();
    row.actorName = actor && actor->GetName() ? actor->GetName() : "?";
    row.armorName = piece.armorName;
    if (flatStart < state.plan.geometries.size()) {
      for (const PieceKey &source :
           KeyChoicesOf(state.plan.geometries[flatStart].keys)) {
        Studio::KeyChoice key;
        key.key = source;
        const RE::TESForm *form = LookupForm(source.form);
        const std::string editorID = form ? EditorIdOf(*form) : std::string{};
        key.text = editorID.empty() ? source.form.ToString() : editorID;
        row.keys.push_back(std::move(key));
      }
      if (full) {
        static_cast<void>(Resolve(state.plan.geometries[flatStart].keys,
                                  LoadedRecipes(), ref.actorID,
                                  &row.selections));
      }
      const Studio::View &view = editor.CurrentView();
      row.previewOverride =
          view.Isolating() || (view.pin && view.pin->piece == ref);
      row.diffusePaths = state.plan.geometries[flatStart].keys.diffusePaths;
      std::ranges::sort(row.diffusePaths);
      const auto duplicates = std::ranges::unique(row.diffusePaths);
      row.diffusePaths.erase(duplicates.begin(), duplicates.end());
    }

    for (const PieceMatch &match : matches) {
      if (auto recipe = BuildRecipe(match)) {
        row.recipes.push_back(std::move(*recipe));
      }
    }
    return row;
  }
};

void AccumulateStatus(Manager::Status &s, const LiveActor &state) {
  s.pieces += static_cast<std::uint32_t>(state.pieces.size());
  s.recipes += static_cast<std::uint32_t>(LiveInstanceCount(state));
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

bool ContainsPiece(const LiveActor &state, RE::FormID actorID,
                   const Studio::PieceRef &request) {
  std::size_t flatBase = 0;
  for (const LivePiece &piece : state.pieces) {
    const bool firstPerson = flatBase < state.plan.geometries.size() &&
                             state.plan.geometries[flatBase].firstPerson;
    if (request == Studio::PieceRef{actorID, piece.armor, firstPerson}) {
      return true;
    }
    flatBase += piece.geometries.size();
  }
  return false;
}

void AppendLoadedRecipes(Manager::Snapshot &snapshot) {
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
    snapshot.loaded.push_back(recipe.id);
    snapshot.loadedRecipes.push_back(std::move(row));
  }
}

}

Manager::Status Manager::GetStatus() const {
  Status s;
  s.emissivePath = emissivePathEnabled_;
  s.layoutVerified = layoutVerified_;
  s.textureLab = TextureLab::GetSingleton()->Available();
  s.actors = static_cast<std::uint32_t>(applied_.size());
  for (const auto &[id, state] : applied_) {
    AccumulateStatus(s, state);
  }
  s.tickMS = GetSettings().TickIntervalMS();
  return s;
}

void Manager::Watch(const std::optional<Studio::PieceRef> &a_request,
                    std::string_view a_document) {
  std::scoped_lock lock{snapshotLock_};
  watch_ = a_request;
  watchedDocument_ = a_document;
  watchedMS_ = NowMS();
}

std::shared_ptr<const Manager::Snapshot> Manager::LatestSnapshot() const {
  std::scoped_lock lock{snapshotLock_};
  return latest_;
}

void Manager::PublishSnapshot(std::uint32_t a_nowMS) {
  std::optional<Studio::PieceRef> request;
  std::string document;
  {
    std::scoped_lock lock{snapshotLock_};
    if (watchedMS_ == 0 || a_nowMS > watchedMS_ + kWatchWindowMS) {
      return;
    }
    request = watch_;
    document = watchedDocument_;
  }
  auto built = std::make_shared<Snapshot>(BuildSnapshot(request, document));
  built->version = ++snapshotVersion_;
  built->view = editor_.CurrentView();
  std::scoped_lock lock{snapshotLock_};
  latest_ = std::move(built);
}

Manager::Snapshot
Manager::BuildSnapshot(const std::optional<Studio::PieceRef> &a_request,
                       std::string_view a_document) const {
  Snapshot out;
  PublishStatus(out);
  PublishPieces(out, a_request);
  PublishCatalogs(out, a_request ? a_request->actorID : 0);
  AppendLoadedRecipes(out);
  PublishDocument(out, a_document);
  return out;
}

void Manager::PublishStatus(Snapshot &a_out) const {
  a_out.applications = applications_.Snapshot();
  a_out.fileOperations = editor_.FileOperations();
  a_out.editResults = editor_.EditResults();
  a_out.gesture = editor_.LastGesture();
  a_out.paintCommit = editor_.LastPaintCommit();
  a_out.paintUpdate = editor_.LastPaintUpdate();
  const Status status = GetStatus();
  const RecipeStoreStatus store = GetRecipeStoreStatus();
  a_out.tickMS = status.tickMS;
  a_out.status = {status.emissivePath, status.layoutVerified, status.textureLab,
                  status.actors,       status.pieces,         status.recipes,
                  status.geometries,   status.shells,         status.lights,
                  store.loaded,        store.withErrors};
}

void Manager::PublishPieces(
    Snapshot &a_out, const std::optional<Studio::PieceRef> &a_request) const {
  const bool anyMatch =
      a_request && std::ranges::any_of(applied_, [&](const auto &entry) {
        return ContainsPiece(entry.second, entry.first, *a_request);
      });

  bool first = true;
  for (const auto &[actorID, state] : applied_) {
    std::size_t flatBase = 0;
    for (const LivePiece &piece : state.pieces) {
      const std::size_t flatStart = flatBase;
      const std::size_t geomCount = piece.geometries.size();
      flatBase += geomCount;

      const std::vector<PieceMatch> matches =
          MatchesForPiece(state.plan, GeometryId{flatStart}, geomCount);
      if (matches.empty()) {
        continue;
      }
      const bool firstPerson = flatStart < state.plan.geometries.size() &&
                               state.plan.geometries[flatStart].firstPerson;
      const Studio::PieceRef ref{actorID, piece.armor, firstPerson};
      const bool full = anyMatch ? (a_request && *a_request == ref) : first;
      first = false;

      a_out.pieces.push_back(PieceSnapshotBuilder{a_out, editor_, state, piece,
                                                  ref, flatStart, full}
                                 .Build(matches));
    }
  }
}

void Manager::PublishDocument(Snapshot &a_out,
                              std::string_view a_document) const {
  if (a_document.empty()) {
    return;
  }
  const std::span<const Recipe> loaded = LoadedRecipes();
  if (const Recipe *document = FindById(loaded, a_document)) {
    a_out.documents.push_back(DocumentRowFor(*document));
  }
}

Studio::RecipeRow Manager::DocumentRowFor(const Recipe &a_document) const {
  const Studio::ReferenceCounts *references = ReferencesOf(a_document.id);
  const Studio::ReferenceCounts emptyReferences;
  const std::shared_ptr<const RecipeGraph> graph = GraphFor(a_document);
  const std::optional<RecipeOrigin> origin = OriginOf(a_document);
  const Studio::History<Recipe> *history = editor_.HistoryOf(a_document.id);
  const RecipeKey key =
      a_document.keys.empty() ? RecipeKey{} : a_document.keys.front();
  Studio::RecipeRow row = Studio::BuildRecipeRow(
      {.recipe = a_document,
       .key = key,
       .priority = a_document.priority.value_or(DefaultPriority(key.kind)),
       .lightOutput = std::nullopt,
       .dirty = IsDirty(a_document.id),
       .full = true,
       .undoDepth = history ? history->UndoDepth() : 0,
       .redoDepth = history ? history->RedoDepth() : 0,
       .references = references ? *references : emptyReferences,
       .graph = graph.get(),
       .problems =
           origin ? origin->diagnostics : std::span<const Diagnostic>{}});
  row.documentRevision = editor_.DocumentRevisionOf(a_document.id);
  return row;
}
}
