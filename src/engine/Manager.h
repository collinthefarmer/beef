// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "Settings.h"
#include "actors/Actor.h"
#include "actors/SyncLog.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/WarningHistory.h"
#include "engine/ActorObservation.h"
#include "engine/AnimationSubscriptions.h"
#include "engine/ApplicationService.h"
#include "engine/IdleCamera.h"
#include "engine/InstanceTime.h"
#include "engine/LiveActor.h"
#include "engine/PlanJobs.h"
#include "engine/RecipeEditor.h"
#include "planners/ActorPlanning.h"
#include "planners/BudgetRadius.h"
#include "planners/DrawPacing.h"
#include "planners/InstallPacing.h"
#include "planners/RenderBudget.h"
#include "planners/RenderPlanCache.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"
#include "recipe/Recipe.h"
#include "regression/Run.h"
#include "render/TextureRef.h"
#include "render/VideoMemory.h"
#include "studio/BudgetView.h"
#include "studio/Intent.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
struct ActorStacks;
struct ActorRender;

struct RegressionWearer {
  RE::FormID actor = 0;
  RE::FormID armor = 0;
};

struct WearerFacts {
  bool live = false;
  bool inView = true;
  bool awaitingView = false;
  std::vector<RE::FormID> liveItems;
  std::uint64_t renderedAttempt = 0;
  std::string application;
};

class Manager {
public:
  [[nodiscard]] static Manager *GetSingleton();

  void MarkStale(RE::FormID a_actorID);
  void MarkStale(RE::Actor *a_actor);
  void QueueForget(RE::FormID a_actorID);
  void QueueEquipFinalize(RE::FormID a_actorID);
  void MarkLoadedActorsStale();

  void Clear();
  void BeginLoad();
  void FinishLoad();
  void ReapplyAll();
  void RetireAll();

  void FireAt(Studio::FireTrigger a_trigger);
  void RequestMesh(RE::FormID a_actorID, std::string a_geometry);

  void OnFrame();
  void FlushMetricsHeartbeat();
  void QueueRegression(std::uint64_t a_request);
  void SoloInEditor(std::string a_recipe);
  void EndSoloInEditor();
  void ShowRecipeSetInEditor(std::vector<std::string> a_recipes);
  void ObserveRegression();
  [[nodiscard]] std::vector<WearerFacts>
  RegressionActors(std::span<const RegressionWearer> a_wearers) const;
  [[nodiscard]] Regression::Activity
  RegressionActivity(std::uint64_t a_edit, std::uint64_t a_gesture,
                     std::uint64_t a_file) const;
  [[nodiscard]] Regression::RecipeFacts
  RegressionRecipe(std::string_view a_recipe) const;
  [[nodiscard]] std::uint64_t StartRegressionEdit(const std::string &a_recipe,
                                                  float a_opacity);
  [[nodiscard]] std::uint64_t
  StartRegressionDuplicate(const std::string &a_from, const std::string &a_to);
  [[nodiscard]] std::uint64_t StartRegressionSave(const std::string &a_recipe);
  [[nodiscard]] std::uint64_t
  StartRegressionDelete(const std::string &a_recipe);
  [[nodiscard]] std::uint64_t
  StartRegressionGesture(const std::string &a_recipe);
  void StartRegressionPaint(const std::string &a_recipe);

  void Fire(RE::FormID a_actorID, const EventRecord &a_event);
  void QueueEvent(RE::FormID a_actorID, EventRecord a_event);
  void QueueBroadcast(EventRecord a_event);

  void SetEmissivePathEnabled(bool a_enabled);
  void HoldIdleCameraForMenu();
  void ReleaseIdleCameraForMenu();

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
  [[nodiscard]] std::shared_ptr<const Studio::BudgetView>
  LatestBudgetView() const;

private:
  std::uint64_t renderFrame_ = 0;
  std::uint32_t tickNowMS_ = 0;
  friend class RecipeEditor;

  Manager();
  void PostTask(std::function<void()> a_task);
  [[nodiscard]] std::uint32_t EditorClockMS() const;

  void SyncStale(RE::FormID a_actorID,
                 const std::vector<ApplicationToken> &a_tokens);
  enum class SyncOutcome { kSettled, kInstalled, kDeferred };
  [[nodiscard]] SyncOutcome SyncActor(RE::Actor *a_actor);
  void DeferInstall(RE::FormID a_actorID, Actors::SyncKind a_kind);
  void RunWaitingInstalls(std::uint32_t a_nowMS);
  struct CatalogMatches {
    std::shared_ptr<const RecipeCatalog> catalog;
    ActorMatches matches;
  };
  struct ActorDecision {
    Actors::ActorFacts facts;
    CatalogMatches planned;
    Actors::Transition next;
    Actors::SyncKind kind = Actors::SyncKind::kUnchanged;
    std::uint32_t observedAt = 0;
  };
  void ReportRenderMemory(const Metrics::Snapshot &a_measured,
                          const TargetCost &a_planned,
                          std::uint32_t a_intervalMs);
  [[nodiscard]] BudgetSample SampleBudget(const Metrics::Snapshot &a_measured,
                                          const Settings &a_settings);
  void UpdateBudget(const BudgetSample &a_sample, std::uint64_t a_planned,
                    const Settings &a_settings);
  struct BudgetActors {
    std::vector<BudgetActor> actors;
    bool resizeBusy = false;
  };
  struct BlockedStepUp {
    RE::FormID actor = 0;
    PreemptionRequest request;
  };
  [[nodiscard]] BudgetActors BudgetActorsNow(const Settings &a_settings);
  [[nodiscard]] std::vector<BlockedStepUp>
  BlockedStepUps(const Settings &a_settings);
  void PreemptForBlockedStepUp(const Settings &a_settings);
  void RelieveBudget(std::uint64_t a_goal, const Settings &a_settings);
  void StepDownForBudget(RE::FormID a_actorID, LiveActor &a_state,
                         const StepDownOption &a_option,
                         std::optional<RE::FormID> a_preemptedBy);
  [[nodiscard]] bool ShrinkBudgetRadius(std::span<const BudgetActor> a_actors);
  void GrowBudgetRadius(const Settings &a_settings);
  void ReportRadiusGrowth(const RadiusGrowth &a_growth);
  [[nodiscard]] std::optional<float> FarthestTrackedDistance() const;
  [[nodiscard]] std::vector<EvictedActor> EvictedActors() const;
  [[nodiscard]] std::uint64_t LargestReturnBytes() const;
  [[nodiscard]] float EvictDistanceNow(const Settings &a_settings) const;
  [[nodiscard]] bool AdmitStepUp(RE::FormID a_actorID, LiveActor &a_state,
                                 ResolutionLevel a_from, ResolutionLevel a_to);
  void InstallActor(RE::Actor &a_actor, ActorDecision a_decision,
                    std::uint64_t a_replaced, const Settings &a_settings);
  [[nodiscard]] ActorDecision DecideActor(RE::Actor &a_actor,
                                          const Settings &a_settings);
  void NoteMaterialLayout(MaterialLayout a_before);
  void NoteArmorWithoutGeometry(const Actors::ActorFacts &a_facts);
  void RecordState(RE::FormID a_actorID,
                   std::optional<Actors::ActorState> a_state);
  [[nodiscard]] Actors::ActorPlan InstalledPlanOf(RE::FormID a_actorID) const;
  void MarkWaitingStale(std::uint32_t a_nowMS);
  void MarkRecheckStale(std::uint32_t a_nowMS, float a_evictDistance);
  [[nodiscard]] std::expected<LiveActor, std::string>
  LiveActorFor(RE::Actor &a_actor, const Actors::ActorFacts &a_facts,
               CatalogMatches a_planned, const Settings &a_settings);
  std::size_t RetireActor(RE::FormID a_actorID);
  void ForgetActor(RE::FormID a_actorID);
  void ChangeAndRebuildActors(std::string a_reportRecipe,
                              const std::function<void()> &a_action);
  [[nodiscard]] std::vector<RE::FormID> LoadedActorIDs() const;
  [[nodiscard]] std::vector<RE::FormID> ApplicationActors() const;
  [[nodiscard]] std::vector<RE::FormID>
  ActorsApplying(std::string_view a_recipeID) const;
  void PrepareApplications(RE::FormID a_actor,
                           const std::vector<ApplicationToken> &a_tokens);
  void FinishApplications(RE::FormID a_actor, LiveActor &a_state);
  void AbandonApplications(RE::FormID a_actor);
  void QueueAnimationEvent(AnimationEvent a_event);
  void ReconcileAnimationEvents(RE::FormID a_actor);
  void SweepAnimationEvents();
  void BuildInstances(LiveActor &a_state, const Settings &a_settings);
  void FireEquip(RE::FormID a_actorID);
  void CarryInstanceTime(LiveInstance &a_instance, RE::FormID a_actor,
                         const Recipe &a_recipe, const Settings &a_settings);
  [[nodiscard]] std::optional<std::size_t>
  InstanceFor(LiveActor &a_state, InstanceId a_matchedInstance,
              RE::FormID a_enchantment, const Settings &a_settings);
  [[nodiscard]] InstallBound InstallBoundOf(RE::FormID a_actorID,
                                            RE::Actor &a_actor,
                                            std::uint64_t a_replaced,
                                            const Settings &a_settings) const;
  void KeepInstallLevel(RE::FormID a_actorID, ResolutionLevel a_level);
  void ReleaseWhileHidden(RE::FormID a_actorID, LiveActor &a_state,
                          const Settings &a_settings);
  void RequestLevelStep(RE::FormID a_actorID, RE::Actor &a_actor,
                        LiveActor &a_state, const Settings &a_settings);
  void AdvanceResizes(const Settings &a_settings);
  void PlaceInstances(LiveActor &a_state, RE::FormID a_actorID,
                      std::uint64_t a_replaced, const Settings &a_settings);
  void KeepActorRender(RE::FormID a_actorID, LiveActor &a_state,
                       const ActorRender &a_render);
  void DrainPlanJobs(const Settings &a_settings);
  void RebuildAwaitedRender(RE::FormID a_actorID, LiveActor &a_state,
                            JobId a_arrived, const Settings &a_settings);
  void FailAwaitedRender(RE::FormID a_actorID, LiveActor &a_state,
                         const std::string &a_problem,
                         const Settings &a_settings);
  [[nodiscard]] std::size_t PlaceGeometries(LiveActor &a_state,
                                            const Settings &a_settings,
                                            ActorStacks &a_stacks);
  void PlaceOnGeometry(LiveActor &a_state, GeometryId a_geometry,
                       const Settings &a_settings, ActorStacks &a_stacks);
  void PlaceLightsOf(RE::Actor *a_actor, LiveActor &a_state);

  void Tick(std::uint32_t a_nowMS, const Settings &a_settings);
  void TickInstance(LiveInstance &a_instance, float a_time, float a_delta);
  void DrawHidden(std::span<const HiddenDrawCandidate> a_candidates);
  void AttachFirstDraw(RE::FormID a_actorID, LiveActor &a_state);
  void ClaimLostGeometries(RE::FormID a_actorID, LiveActor &a_state);
  void RenderGeometry(LiveActor &a_state, LiveGeometry &a_bound, bool a_hidden,
                      bool a_inspected);
  [[nodiscard]] std::optional<RE::FormID>
  InspectedActor(std::uint32_t a_nowMS) const;
  [[nodiscard]] std::vector<bool> RenderPieces(LiveActor &a_state,
                                               RE::FormID a_actorID);
  void UpdateLights(LiveActor &a_state,
                    const std::vector<bool> &a_instanceHidden);
  void FireDueFinalizes();

  [[nodiscard]] Snapshot
  BuildSnapshot(const std::optional<Studio::PieceRef> &a_request,
                std::string_view a_document) const;
  void PublishStatus(Snapshot &a_out) const;
  void PublishPieces(Snapshot &a_out,
                     const std::optional<Studio::PieceRef> &a_request) const;
  void PublishDocument(Snapshot &a_out, std::string_view a_document) const;
  [[nodiscard]] Studio::RecipeRow
  DocumentRowFor(const Recipe &a_document) const;
  void PublishSnapshot(std::uint32_t a_nowMS);
  void PublishBudgetView(Studio::BudgetView a_view);

  ApplicationService applications_;
  AnimationSubscriptions animations_;

  std::unordered_map<RE::FormID, LiveActor> live_;
  RecipeEditor editor_{*this};
  std::unordered_set<RE::FormID> loggedNonPBRArmor_;
  WarningHistory warningHistory_;

  CarriedTimes carriedTimes_;
  RenderPlanCache renderPlans_;
  PlanJobBook planJobs_;
  PlanQueue planQueue_{BuildPlanFrom, kPlanJobCapacity};

  struct TrackedActor {
    std::optional<Actors::ActorState> state;
    ModelTable models;
    std::optional<ResolutionLevel> targetResolution;
    std::optional<std::uint64_t> returnBytes;
  };
  std::unordered_map<RE::FormID, TrackedActor> tracked_;
  std::optional<IdleCameraHold> menuIdleHold_;
  std::uint32_t lastWaitingCheckMS_ = 0;
  std::uint64_t regressionPaintSession_ = 0;
  std::uint32_t lastTickMS_ = 0;
  std::uint32_t lastMetricsMS_ = 0;
  std::uint32_t holdingStep_ = 0;
  VideoMemoryProbe videoMemory_;
  BudgetState budget_;
  std::optional<BudgetRadius> budgetRadius_;
  std::vector<Actors::SyncRecord> syncLog_;
  FrameSyncs frameSyncs_;
  std::vector<WaitingInstall> waitingInstalls_;
  std::uint32_t lastSweepMS_ = 0;
  std::uint32_t lastRecheckMS_ = 0;
  bool emissivePathEnabled_ = false;
  MaterialLayout materialLayout_ = MaterialLayout::kUnchecked;
  bool frozenLastTick_ = false;
  bool textureLabStarted_ = false;

  mutable std::mutex snapshotLock_;
  std::shared_ptr<const Snapshot> latest_ = std::make_shared<Snapshot>();
  std::shared_ptr<const Studio::BudgetView> latestBudget_;
  std::optional<Studio::PieceRef> watch_;
  std::string watchedDocument_;
  std::uint32_t watchedMS_ = 0;
  std::uint64_t snapshotVersion_ = 0;
  static constexpr std::uint32_t kWatchWindowMS = 1000;
};
}
