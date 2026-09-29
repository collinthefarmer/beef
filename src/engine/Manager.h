// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "Settings.h"
#include "diagnostics/WarningHistory.h"
#include "engine/AnimationSubscriptions.h"
#include "engine/ApplicationService.h"
#include "engine/InstanceTime.h"
#include "engine/LiveActor.h"
#include "engine/RecipeEditor.h"
#include "planners/ActorPlanning.h"
#include "recipe/Recipe.h"
#include "render/TextureRef.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
struct ActorStacks;
class Manager {
public:
  [[nodiscard]] static Manager *GetSingleton();

  void QueueRefresh(RE::FormID a_actorID);
  void QueueRefresh(RE::Actor *a_actor);
  void QueueRetire(RE::FormID a_actorID);
  void QueueEquipFinalize(RE::FormID a_actorID);
  void QueueLoadedActorRefreshes();

  void Clear();
  void BeginLoad();
  void FinishLoad();
  void ReapplyAll();
  void RetireAll();

  void FireAt(RE::FormID a_actorID, std::string a_event, std::string a_node,
              Vec3 a_offset, float a_random, float a_value);
  void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

  void OnFrame();
  void QueueRegression(std::int32_t a_request);
  void SoloRegressionRecipe(std::string a_recipe);
  void RestoreRegressionView();
  void ObserveRegression();

  void Fire(RE::FormID a_actorID, const EventRecord &a_event);
  void QueueEvent(RE::FormID a_actorID, EventRecord a_event);
  void QueueBroadcast(EventRecord a_event);

  void SetEmissivePathEnabled(bool a_enabled);

  [[nodiscard]] RecipeEditor &Editor() noexcept;

  struct Status {
    bool emissivePath = false;
    bool layoutVerified = false;
    bool textureLab = false;
    std::uint32_t actors = 0;
    std::uint32_t pieces = 0;
    std::uint32_t recipes = 0;
    std::uint32_t geometries = 0;
    std::uint32_t shells = 0;
    std::uint32_t lights = 0;
    std::uint32_t tickMS = 0;
  };
  [[nodiscard]] Status GetStatus() const;

  struct Snapshot : Studio::Snapshot {
    std::vector<TextureRef> textures;
  };
  void Watch(const std::optional<Studio::PieceRef> &a_request,
             std::string_view a_document = {});
  [[nodiscard]] std::shared_ptr<const Snapshot> LatestSnapshot() const;

private:
  std::uint64_t renderFrame_ = 0;
  friend class RecipeEditor;

  Manager();
  void PostTask(std::function<void()> a_task);

  void RunRefresh(RE::FormID a_actorID,
                  const std::vector<ApplicationToken> &a_tokens);
  void Refresh(RE::Actor *a_actor);
  void RetireEffects(RE::FormID a_actorID);
  void Retire(RE::FormID a_actorID);
  void RetireEveryActor();
  void ChangeAndRebuildActors(std::string a_reportRecipe,
                              const std::function<void()> &a_action);
  [[nodiscard]] std::vector<RE::FormID> LoadedActorIDs() const;
  [[nodiscard]] std::vector<RE::FormID> ApplicationActors() const;
  void PrepareApplications(RE::FormID a_actor,
                           const std::vector<ApplicationToken> &a_tokens);
  void FinishApplications(RE::FormID a_actor, LiveActor &a_state);
  void AbandonApplications(RE::FormID a_actor);
  void QueueAnimationEvent(AnimationEvent a_event);
  void ReconcileAnimationEvents(RE::FormID a_actor);
  void SweepAnimationEvents();
  [[nodiscard]] std::vector<LivePiece>
  CollectPieces(RE::Actor *a_actor, bool a_firstPerson,
                const Settings &a_settings);
  bool CollectPieceGeometries(LivePiece &piece, RE::NiAVObject *clone,
                              RE::NiAVObject *root, bool verbose);
  void MatchRecipes(RE::Actor *a_actor, LiveActor &a_state,
                    const Settings &a_settings);
  void FireEquip(RE::FormID a_actorID);
  void CarryInstanceTime(LiveInstance &a_instance, RE::FormID a_actor,
                         const Recipe &a_recipe, const Settings &a_settings);
  [[nodiscard]] std::optional<std::size_t>
  InstanceFor(LiveActor &a_state, InstanceId a_planInstance,
              RE::FormID a_enchantment, const Settings &a_settings);
  void PlaceInstances(LiveActor &a_state, const Settings &a_settings);
  void PlaceOnGeometry(LiveActor &a_state, LivePieceId a_piece,
                       std::size_t a_geometry, GeometryId a_flat,
                       const Settings &a_settings, ActorStacks &a_stacks);
  void PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state,
                     const Settings &a_settings);
  bool LayoutSanityCheck(const PbrMaterial &a_material,
                         std::string_view a_propertyName);

  void Tick(std::uint32_t a_nowMS, const Settings &a_settings);
  void TickInstance(LiveInstance &a_instance, float a_time, float a_delta);
  void DropLostGeometries(LiveActor &a_state);
  void RenderGeometry(LiveActor &a_state, LivePiece &a_piece,
                      LiveGeometry &a_bound, bool a_hidden);
  [[nodiscard]] std::vector<bool> RenderPieces(LiveActor &a_state,
                                               RE::FormID a_actorID);
  void UpdateLights(LiveActor &a_state,
                    const std::vector<bool> &a_instanceHidden);
  void FireDueFinalizes();
  [[nodiscard]] static bool Alive(const LiveActor &a_state) noexcept;

  [[nodiscard]] Snapshot
  BuildSnapshot(const std::optional<Studio::PieceRef> &a_request,
                std::string_view a_document) const;
  void PublishSnapshot(std::uint32_t a_nowMS);

  ApplicationService applications_;
  AnimationSubscriptions animations_;

  std::unordered_map<RE::FormID, LiveActor> applied_;
  RecipeEditor editor_{*this};
  std::unordered_set<RE::FormID> loggedNonPBRArmor_;
  WarningHistory stackWarnings_;

  CarriedTimes carriedTimes_;

  std::unordered_set<RE::FormID> evictedForDistance_;
  void SweepEviction(const Settings &a_settings);
  std::uint32_t lastTickMS_ = 0;
  std::uint32_t lastMetricsMS_ = 0;
  std::uint32_t lastEvictionMS_ = 0;
  bool emissivePathEnabled_ = false;
  bool layoutVerified_ = false;
  bool frozenLastTick_ = false;

  mutable std::mutex snapshotLock_;
  std::shared_ptr<const Snapshot> latest_ = std::make_shared<Snapshot>();
  std::optional<Studio::PieceRef> watch_;
  std::string watchedDocument_;
  std::uint32_t watchedMS_ = 0;
  std::uint64_t snapshotVersion_ = 0;
  static constexpr std::uint32_t kWatchWindowMS = 1000;
};
}
