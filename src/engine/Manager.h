// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "Settings.h"
#include "TraceTypes.h"
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
#include "orchestration/Commands.h"
#include "orchestration/Facts.h"
#include "orchestration/World.h"
#include "planners/ActorPlanning.h"
#include "planners/BudgetRadius.h"
#include "planners/DrawPacing.h"
#include "planners/InstallPacing.h"
#include "planners/InstallProgram.h"
#include "planners/RenderBudget.h"
#include "planners/RenderMemory.h"
#include "planners/RenderPlanCache.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"
#include "planners/TickClock.h"
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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
class TextureLab;

struct RenderNotBuilt {};

struct HeartbeatReading {
  Metrics::Snapshot measured;
  BudgetSample sample;
  std::uint32_t intervalMs = 0;
};

struct RenderFlags {
  bool concealSlots = false;
  bool hideShells = false;
};

using RenderOutcome =
    std::variant<RenderBuild, Orchestration::PlanAwaited, RenderNotBuilt>;

struct PlacedRender {
  RenderOutcome built;
  std::vector<GeometrySlot> unpublished;
};

using CommandResults = std::vector<Orchestration::CommandResult>;

struct GatheredFacts {
  Orchestration::TickFacts facts;
  std::map<RE::FormID, std::vector<ApplicationToken>> tokens;
  std::shared_ptr<const RecipeCatalog> catalog;
  std::vector<WaitingInstall> waiting;
  std::uint32_t observedAtMS = 0;
};

struct TickContext {
  const Settings &settings;
  const Studio::View &view;
  std::optional<RE::FormID> inspected;
  std::uint32_t nowMS = 0;
  bool resuming = false;
  const GatheredFacts &inputs;
};

struct RegressionWearer {
  RE::FormID actor = 0;
  RE::FormID armor = 0;
};

struct WearerFacts {
  bool live = false;
  bool shown = true;
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
  [[nodiscard]] bool IsSettling(std::string_view a_recipeID) const;

  void NoteStale(RE::FormID a_actorID,
                 const std::vector<ApplicationToken> &a_tokens);
  [[nodiscard]] std::optional<HeartbeatReading> RunFrame(TextureLab &a_lab,
                                                         std::uint32_t a_nowMS);
  [[nodiscard]] std::optional<HeartbeatReading>
  RunTick(TextureLab &a_lab, std::uint32_t a_nowMS, const Settings &a_settings);
  [[nodiscard]] GatheredFacts
  GatherFacts(std::uint32_t a_nowMS, const Settings &a_settings,
              std::vector<Orchestration::PlanJobDone> a_jobs);
  [[nodiscard]] Orchestration::Sighting
  SightingOf(RE::FormID a_actorID, const Settings &a_settings,
             std::optional<RE::FormID> a_inspected) const;
  [[nodiscard]] Orchestration::StaleActor
  StaleActorOf(RE::FormID a_actorID, const RecipeCatalog &a_catalog,
               const Settings &a_settings, std::uint32_t a_observedAtMS);
  [[nodiscard]] std::vector<Orchestration::ClaimedGeometry> ClaimsOf();
  [[nodiscard]] std::vector<Orchestration::ActorId> ForgetRequests();
  void CheckAgreement(const Orchestration::TickFacts &a_facts);
  void ReportDisagreement(RE::FormID a_actorID, Trace::Field a_where,
                          std::string a_what);
  void EmitCoreRecords(const std::vector<Orchestration::TraceRecord> &a_trace,
                       const std::vector<Orchestration::LogRecord> &a_logs);
  [[nodiscard]] CommandResults
  Execute(const std::vector<Orchestration::Command> &a_commands,
          const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunTickActor(const Orchestration::TickActor &a_command,
               const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunStartResize(const Orchestration::StartResize &a_command);
  [[nodiscard]] CommandResults
  RunSwapResize(const Orchestration::SwapResize &a_command,
                const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunAbandonResize(const Orchestration::AbandonResize &a_command);
  [[nodiscard]] CommandResults
  RunReleaseIntermediates(const Orchestration::ReleaseIntermediates &a_command);
  [[nodiscard]] CommandResults
  RunSetAppliedLevel(const Orchestration::SetAppliedLevel &a_command);
  [[nodiscard]] CommandResults
  RunDrawUnpublished(const Orchestration::DrawUnpublished &a_command,
                     const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunBuildAwaitedRender(const Orchestration::BuildAwaitedRender &a_command,
                        const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunFailAwaitedRender(const Orchestration::FailAwaitedRender &a_command);
  [[nodiscard]] CommandResults
  RunCancelPlanJob(const Orchestration::CancelPlanJob &a_command);
  [[nodiscard]] CommandResults
  RunInstall(const Orchestration::Install &a_command,
             const TickContext &a_context);
  [[nodiscard]] CommandResults RunRetire(const Orchestration::Retire &a_command,
                                         const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunForget(const Orchestration::Forget &a_command);
  [[nodiscard]] CommandResults
  RunObserve(const Orchestration::Observe &a_command);
  [[nodiscard]] CommandResults
  RunKeepModels(const Orchestration::KeepModels &a_command,
                const TickContext &a_context);
  [[nodiscard]] CommandResults
  RunSettleApplications(const Orchestration::SettleApplications &a_command,
                        const TickContext &a_context);
  [[nodiscard]] Orchestration::CommandResult
  InstallLiveActor(RE::Actor &a_actor, const Orchestration::Install &a_command,
                   const TickContext &a_context);
  [[nodiscard]] Orchestration::RenderEvent
  BuildAwaitedRender(RE::FormID a_actorID, LiveActor &a_state,
                     const InstallBounds &a_bounds, const Settings &a_settings);
  [[nodiscard]] std::size_t RetireActorResources(RE::FormID a_actorID);
  void ForgetActorResources(RE::FormID a_actorID);
  struct CatalogMatches {
    std::shared_ptr<const RecipeCatalog> catalog;
    ActorMatches matches;
  };
  void EmitHeartbeat(const HeartbeatReading &a_heartbeat);
  [[nodiscard]] std::vector<ActorTargets> RenderedTargets() const;
  void ReportRenderMemory(const Metrics::Snapshot &a_measured,
                          const TargetCost &a_planned,
                          std::uint32_t a_intervalMs);
  [[nodiscard]] BudgetSample SampleBudget(const Metrics::Snapshot &a_measured,
                                          const Settings &a_settings);
  void NoteMaterialLayout(MaterialLayout a_before);
  void NoteArmorWithoutGeometry(const Actors::ActorFacts &a_facts);
  [[nodiscard]] std::expected<LiveActor, std::string>
  LiveActorFor(RE::Actor &a_actor, const Actors::ActorFacts &a_facts,
               const ModelTable &a_models, CatalogMatches a_planned,
               const Settings &a_settings);
  void ChangeAndRebuildActors(std::string a_reportRecipe,
                              const std::function<void()> &a_action);
  [[nodiscard]] std::vector<RE::FormID> LoadedActorIDs() const;
  [[nodiscard]] std::vector<RE::FormID> ApplicationActors() const;
  [[nodiscard]] std::vector<RE::FormID>
  ActorsApplying(std::string_view a_recipeID) const;
  void PrepareApplications(RE::FormID a_actor,
                           const std::vector<ApplicationToken> &a_tokens);
  void FinishApplications(RE::FormID a_actor, LiveActor &a_state,
                          bool a_inView);
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
  [[nodiscard]] PlacedRender PlaceInstances(LiveActor &a_state,
                                            RE::FormID a_actorID,
                                            const InstallBounds &a_bounds,
                                            std::uint64_t a_replaced,
                                            const Settings &a_settings);
  [[nodiscard]] std::vector<Orchestration::PlanJobDone> DrainPlanJobs();
  void FailAwaitedRender(LiveActor &a_state, const std::string &a_problem);
  [[nodiscard]] OutputFilter ShownOutputs() const;

  void TickInstance(LiveInstance &a_instance, float a_time, float a_delta);
  void RenderGeometry(LiveActor &a_state, LiveGeometry &a_bound,
                      bool a_pieceHidden, bool a_inspected,
                      const RenderFlags &a_flags);
  [[nodiscard]] std::optional<RE::FormID>
  InspectedActor(std::uint32_t a_nowMS) const;
  [[nodiscard]] std::vector<bool> RenderPieces(LiveActor &a_state,
                                               RE::FormID a_actorID,
                                               const RenderFlags &a_flags);
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

  std::optional<IdleCameraHold> menuIdleHold_;
  std::uint64_t regressionPaintSession_ = 0;
  std::uint32_t lastTickMS_ = 0;
  std::uint32_t lastMetricsMS_ = 0;
  std::uint32_t holdingStep_ = 0;
  VideoMemoryProbe videoMemory_;
  std::uint32_t lastSweepMS_ = 0;
  Orchestration::World world_;
  std::unordered_map<RE::FormID, ModelTable> models_;
  std::vector<Orchestration::CommandResult> results_;
  std::map<RE::FormID, std::vector<ApplicationToken>> stale_;
  std::set<RE::FormID> forgets_;
  bool heartbeatDue_ = false;
  std::optional<BudgetSample> pendingSample_;
  TickClock tickClock_;
  std::map<RE::FormID, std::vector<std::string>> disagreements_;
  std::uint64_t disagreementCount_ = 0;
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
