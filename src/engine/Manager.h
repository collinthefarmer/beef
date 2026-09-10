#pragma once

#include "PCH.h"
#include "engine/Environment.h"
#include "planners/ActorState.h"
#include "planners/BindingDiff.h"
#include "planners/ManagerDecisions.h"
#include "planners/StackPlan.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "render/Binding.h"
#include "render/Compositor.h"
#include "studio/Edits.h"
#include "studio/History.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
struct PlacedOutput {
  std::size_t index = 0;
  std::unique_ptr<RenderedStack> stack;
  std::string problem;
};

struct LiveGeometry {
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::BSLightingShaderProperty> property;
  std::string name;
  GeometryInputs inputs;
  std::unique_ptr<MaterialBinding> material;
  std::unique_ptr<ShellBinding> shell;
  std::optional<std::size_t> shellOwner;
  std::vector<PlacementId> placements;
  GeometryPlan plan;
  GeometryStackPlan stackPlan;
  BindingDiff binding;
  bool lost = false;
};

struct LivePiece {
  RE::FormID armor = 0;
  std::string armorName;
  RE::MagicItem *enchantment = nullptr;
  std::vector<LiveGeometry> geometries;
};

struct LiveInstance {
  const Recipe *recipe = nullptr;
  RE::FormID enchantment = 0;
  int priority = 0;
  std::shared_ptr<const SignalGraph> graph;
  std::unique_ptr<SignalState> signals;
  std::unique_ptr<ActorEnvironment> environment;
  std::unique_ptr<LightBinding> light;
  std::optional<std::size_t> lightOutput;
  std::uint32_t startMS = 0;
  float lastTime = 0.0f;
};

struct LivePlacement {
  std::size_t geometry = 0;
  std::vector<PlacedOutput> outputs;
};

struct LiveActor {
  ActorState structure;
  std::vector<LivePiece> pieces;
  std::vector<LiveInstance> instances;
  std::vector<LivePlacement> placements;
};

class Manager {
public:
  [[nodiscard]] static Manager *GetSingleton();

  void QueueRefresh(RE::FormID a_actorID);
  void QueueRefresh(RE::Actor *a_actor);
  void QueueRetire(RE::FormID a_actorID);
  void QueueEquipFinalize(RE::FormID a_actorID);
  void QueueLoadedActorRefreshes();

  void Clear();
  void ReapplyAll();
  void Isolate(std::string a_recipe, int a_output, int a_layer);
  void PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID);
  void RetireAll();

  void EditRecipe(std::string a_id, Studio::EditBatch a_edits);
  void UndoRecipe(std::string a_id);
  void RedoRecipe(std::string a_id);
  void SaveRecipe(std::string a_id);
  void RevertRecipe(std::string a_id);
  void ReloadRecipes();
  void NewRecipe(std::string a_id, RecipeKey a_key, std::string a_geometry);
  void RenameRecipe(std::string a_from, std::string a_to);
  void BeginPaint(std::string a_active, RecipeKey a_key, Surface a_surface);
  void SetPaintSurface(Surface a_surface);
  void KeepPaint(std::string a_active, std::string a_name);
  void EndPaint();
  void FireAt(RE::FormID a_actorID, std::string a_event, std::string a_node,
              Vec3 a_offset, float a_random, float a_value);
  void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

  void OnFrame();

  void Fire(RE::FormID a_actorID, const EventRecord &a_event);
  void QueueEvent(RE::FormID a_actorID, EventRecord a_event);

  void SetEmissivePathEnabled(bool a_enabled);

  void UpdateView(std::function<void(Studio::View &)> a_change);

  struct Status {
    bool emissivePath = false;
    bool layoutVerified = false;
    bool runtimeLab = false;
    std::uint32_t actors = 0;
    std::uint32_t pieces = 0;
    std::uint32_t recipes = 0;
    std::uint32_t geometries = 0;
    std::uint32_t shells = 0;
    std::uint32_t lights = 0;
    std::uint32_t tickMS = 0;
  };
  [[nodiscard]] Status GetStatus() const;

  using Snapshot = Studio::Snapshot;
  void Watch(const std::optional<Studio::PieceRef> &a_request);
  [[nodiscard]] std::shared_ptr<const Snapshot> LatestSnapshot() const;

private:
  void PostTask(std::function<void()> a_task);

  void RunRefresh(RE::FormID a_actorID, std::uint64_t a_generation);
  void Refresh(RE::Actor *a_actor);
  void Retire(RE::FormID a_actorID);
  void RetireEveryActor();
  void WithRecipeRetired(std::string_view a_id,
                         const std::function<void()> &a_action);
  void WithListMoved(const std::function<void()> &a_action);
  [[nodiscard]] std::vector<LivePiece> CollectPieces(RE::Actor *a_actor,
                                                     bool a_firstPerson);
  void MatchRecipes(RE::Actor *a_actor, LiveActor &a_state);
  [[nodiscard]] std::optional<std::size_t>
  InstanceFor(RE::Actor *a_actor, LiveActor &a_state, RecipeId a_recipe,
              RE::MagicItem *a_enchantment);
  void PlaceInstances(RE::Actor *a_actor, LiveActor &a_state);
  void PlaceOnGeometry(RE::Actor *a_actor, LiveActor &a_state, PieceId a_piece,
                       std::size_t a_geometry);
  void PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state);
  bool LayoutSanityCheck(RE::BSLightingShaderProperty *a_property);

  void Tick(std::uint32_t a_nowMS);
  void TickInstance(LiveInstance &a_instance, float a_time, float a_delta);
  void DropLostGeometries(LiveActor &a_state);
  void RenderGeometry(LiveActor &a_state, LivePiece &a_piece,
                      LiveGeometry &a_bound);
  void UpdateLights(LiveActor &a_state);
  void FireDueFinalizes();
  [[nodiscard]] static bool Alive(const LiveActor &a_state) noexcept;

  void ApplyEdits(const std::string &a_id, const Studio::EditBatch &a_edits);
  void RestoreRecipe(const std::string &a_id, bool a_redo);

  [[nodiscard]] Snapshot
  BuildSnapshot(const std::optional<Studio::PieceRef> &a_request) const;
  void PublishSnapshot(std::uint32_t a_nowMS);

  std::mutex queueLock_;
  std::unordered_set<RE::FormID> pending_;
  std::unordered_set<RE::FormID> rerun_;
  std::unordered_map<RE::FormID, std::uint32_t> finalizeDue_;
  std::unordered_set<RE::FormID> equipped_;
  std::atomic<std::uint64_t> generation_{0};

  std::unordered_map<RE::FormID, LiveActor> applied_;
  std::unordered_map<std::string, Studio::History<Recipe>> histories_;
  std::unordered_set<RE::FormID> loggedNonPBRArmor_;

  struct CarriedTime {
    float seconds = 0.0f;
    std::uint32_t retiredMS = 0;
  };
  std::map<std::pair<RE::FormID, std::string>, CarriedTime> carriedTimes_;
  static constexpr std::uint32_t kCarryWindowMS = 2000;

  std::uint32_t lastTickMS_ = 0;
  bool emissivePathEnabled_ = true;
  bool layoutVerified_ = false;
  bool frozenLastTick_ = false;
  Studio::View view_{};

  struct IsolateState {
    std::string recipeID;
    int output = -1;
    int layer = -1;
    bool bySolo = false;
  };
  IsolateState paintReturn_{};

  mutable std::mutex snapshotLock_;
  std::shared_ptr<const Snapshot> latest_ = std::make_shared<Snapshot>();
  std::optional<Studio::PieceRef> watch_;
  std::uint32_t watchedMS_ = 0;
  std::uint64_t snapshotVersion_ = 0;
  static constexpr std::uint32_t kWatchWindowMS = 1000;
};
}
