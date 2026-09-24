#include "engine/ApplicationService.h"
#include "engine/SessionQueue.h"
#include "test_support.h"

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Scheduler {
  bool accept = true;
  std::deque<SessionQueue::Task> tasks;

  bool Submit(SessionQueue::Task a_task) {
    if (!accept) {
      return false;
    }
    tasks.push_back(std::move(a_task));
    return true;
  }

  void RunOne() {
    if (tasks.empty()) {
      return;
    }
    SessionQueue::Task task = std::move(tasks.front());
    tasks.pop_front();
    task();
  }

  void Drain() {
    for (std::size_t i = 0; i < 32 && !tasks.empty(); ++i) {
      RunOne();
    }
    Check(tasks.empty(),
          "applicator scheduler drains a bounded amount of work");
  }
};

struct Applied {
  std::uint32_t actorID = 0;
  std::vector<ApplicationToken> tokens;
  bool equipped = false;
};
}

namespace {
struct Flow {
  Scheduler scheduler;
  std::vector<Applied> calls;
  ApplicationService service{
      [this](SessionQueue::Task a_task) {
        return scheduler.Submit(std::move(a_task));
      },
      [this](std::uint32_t a_actor,
             const std::vector<ApplicationToken> &a_tokens) {
        calls.push_back(
            Applied{a_actor, a_tokens, service.TakeEquipped(a_actor)});
        for (const ApplicationToken &token : a_tokens) {
          service.Report(token, a_actor, ApplicationPhase::kPrepared);
        }
      }};

  Flow() { service.Resume(); }

  std::optional<ApplicationRecord> ActorRecord(std::uint32_t a_actor) const {
    for (const ApplicationRecord &record : service.Snapshot()) {
      if (record.token.actorID == a_actor) {
        return record;
      }
    }
    return std::nullopt;
  }

  std::optional<ApplicationRecord>
  RecipeRecord(std::string_view a_recipe) const {
    for (const ApplicationRecord &record : service.Snapshot()) {
      if (record.token.actorID == 0 && record.token.recipeID == a_recipe) {
        return record;
      }
    }
    return std::nullopt;
  }

  void Render(const Applied &a_applied) {
    for (const ApplicationToken &token : a_applied.tokens) {
      service.Report(token, a_applied.actorID, ApplicationPhase::kRendered);
    }
  }
};

void OrdinaryRefreshAndFirstRender() {
  Flow flow;
  Check(flow.service.Refresh(42),
        "ordinary refresh is accepted through the applicator");
  Check(flow.calls.empty() && flow.scheduler.tasks.size() == 1,
        "ordinary refresh applies only when its owning task runs");
  flow.scheduler.Drain();
  Check(flow.calls.size() == 1 && flow.calls.front().actorID == 42 &&
            !flow.calls.front().tokens.empty(),
        "an event refresh with no recipe request enters the adapter with "
        "tracked tokens");
  const auto prepared = flow.ActorRecord(42);
  Check(prepared && prepared->phase == ApplicationPhase::kPrepared &&
            prepared->token.actorID == 42,
        "ordinary refresh publishes its actor preparation result");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.front());
  }
  const auto rendered = flow.ActorRecord(42);
  Check(rendered && rendered->phase == ApplicationPhase::kRendered,
        "ordinary first render acknowledges the same lifecycle application");
}

void EquipFinalize() {
  Flow flow;
  flow.service.Equip(17, 1000);
  flow.service.FinalizeDue(1099);
  Check(flow.scheduler.tasks.empty(),
        "equip finalization waits for the configured debounce");
  flow.service.FinalizeDue(1100);
  flow.scheduler.Drain();
  Check(flow.calls.size() == 1 && flow.calls.front().actorID == 17 &&
            flow.calls.front().equipped && !flow.calls.front().tokens.empty(),
        "equip finalization enters the applicator with an equip event and "
        "tracked tokens");
  Check(!flow.service.TakeEquipped(17),
        "the adapter consumes the equip event once");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.front());
  }
  const auto record = flow.ActorRecord(17);
  Check(record && record->phase == ApplicationPhase::kRendered,
        "equip first-render outcome is visible in application records");
}

void CoalescedRerunAndStaleActorResult() {
  Flow flow;
  flow.service.Refresh(7);
  flow.service.Refresh(7);
  flow.service.Refresh(7);
  Check(flow.scheduler.tasks.size() == 1,
        "ordinary refresh bursts coalesce inside the applicator");
  flow.scheduler.RunOne();
  const auto first = flow.ActorRecord(7);
  Check(first && flow.calls.size() == 1 && flow.scheduler.tasks.size() == 1,
        "coalesced refresh performs once and schedules a tracked follow-up");
  flow.scheduler.RunOne();
  const auto second = flow.ActorRecord(7);
  Check(first && second && second->token.revision > first->token.revision &&
            flow.calls.size() == 2 && !flow.calls.back().tokens.empty(),
        "each actual coalesced rerun receives a fresh actor revision");
  if (first) {
    flow.service.Report(first->token, 7, ApplicationPhase::kRendered);
  }
  const auto afterStale = flow.ActorRecord(7);
  Check(second && afterStale && afterStale->token == second->token &&
            afterStale->phase == ApplicationPhase::kPrepared,
        "the first refresh cannot acknowledge the newer actor revision");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.back());
  }
  const auto complete = flow.ActorRecord(7);
  Check(complete && complete->phase == ApplicationPhase::kRendered &&
            flow.scheduler.tasks.empty(),
        "the final coalesced application completes without an endless rerun");
}

void IndependentActorsAndRecipeOverlap() {
  Flow flow;
  flow.service.Refresh(11);
  flow.service.Refresh(22);
  flow.scheduler.Drain();
  const auto actor22 = flow.ActorRecord(22);
  flow.service.Refresh(11);
  flow.scheduler.Drain();
  const auto preserved = flow.ActorRecord(22);
  Check(actor22 && preserved && actor22->token == preserved->token &&
            preserved->phase == ApplicationPhase::kPrepared,
        "refreshing one actor preserves another actor's pending record");

  const auto recipe = flow.service.Begin("paint", {11});
  flow.service.Refresh(11);
  flow.service.Refresh(11);
  flow.scheduler.Drain();
  Check(!flow.calls.empty() &&
            std::ranges::any_of(flow.calls.back().tokens,
                                [&](const ApplicationToken &a_token) {
                                  return a_token.recipeID == recipe.recipeID &&
                                         a_token.revision == recipe.revision &&
                                         a_token.actorID == 0;
                                }) &&
            std::ranges::any_of(flow.calls.back().tokens,
                                [](const ApplicationToken &a_token) {
                                  return a_token.actorID == 11;
                                }),
        "a recipe request overlapping an event refresh captures recipe and "
        "lifecycle tokens together");
  const auto pendingRecipe = flow.RecipeRecord("paint");
  Check(pendingRecipe && pendingRecipe->phase == ApplicationPhase::kPrepared,
        "overlapping lifecycle work does not cancel the recipe request");
  const Applied previous = flow.calls.empty() ? Applied{} : flow.calls.back();
  flow.service.Refresh(11);
  flow.scheduler.Drain();
  flow.Render(previous);
  const auto afterStaleRecipe = flow.RecipeRecord("paint");
  const auto afterStaleActor = flow.ActorRecord(11);
  Check(afterStaleRecipe && afterStaleActor &&
            afterStaleRecipe->phase == ApplicationPhase::kPrepared &&
            afterStaleActor->phase == ApplicationPhase::kPrepared,
        "old scoped and lifecycle completions cannot acknowledge a newer actor "
        "refresh attempt");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.back());
  }
  const auto renderedRecipe = flow.RecipeRecord("paint");
  const auto renderedActor = flow.ActorRecord(11);
  Check(renderedRecipe && renderedActor &&
            renderedRecipe->phase == ApplicationPhase::kRendered &&
            renderedActor->phase == ApplicationPhase::kRendered,
        "one authoritative render completes both scoped and lifecycle "
        "application records");
  const auto stillPreserved = flow.ActorRecord(22);
  Check(actor22 && stillPreserved && stillPreserved->token == actor22->token &&
            stillPreserved->phase == ApplicationPhase::kPrepared,
        "recipe refreshes on actor 11 cannot cancel unrelated actor 22");
}

void LoadCancellationAndLoadedRefresh() {
  Flow flow;
  flow.service.Refresh(31);
  flow.scheduler.Drain();
  const auto old = flow.ActorRecord(31);
  flow.service.Refresh(32);
  flow.service.Equip(33, 0);
  flow.service.BeginLoad();
  Check(flow.service.Loading(), "the applicator owns the paused load state");
  const auto cancelled = flow.ActorRecord(31);
  Check(cancelled && cancelled->phase == ApplicationPhase::kCancelled,
        "load cancels a lifecycle application waiting for first render");
  Check(!flow.service.Refresh(34), "refreshes cannot run during a load");
  flow.service.Resume();
  flow.service.Refresh(31);
  flow.service.FinalizeDue(100);
  flow.scheduler.Drain();
  Check(flow.calls.size() == 2 && flow.calls.back().actorID == 31 &&
            !flow.calls.back().tokens.empty(),
        "loaded-actor refresh is tracked while old queued and equip callbacks "
        "are discarded");
  if (old) {
    flow.service.Report(old->token, 31, ApplicationPhase::kRendered);
  }
  const auto current = flow.ActorRecord(31);
  Check(
      old && current && current->token.revision > old->token.revision &&
          current->phase == ApplicationPhase::kPrepared,
      "a pre-load actor result cannot acknowledge the new load's application");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.back());
  }
  const auto complete = flow.ActorRecord(31);
  Check(complete && complete->phase == ApplicationPhase::kRendered,
        "the loaded actor reaches rendered through the same service route");
}

void RejectedInternalScheduling() {
  Flow equip;
  equip.service.Equip(51, 0);
  equip.scheduler.accept = false;
  equip.service.FinalizeDue(100);
  const auto failedEquip = equip.ActorRecord(51);
  Check(failedEquip && failedEquip->phase == ApplicationPhase::kFailed &&
            equip.calls.empty() && equip.scheduler.tasks.empty(),
        "equip finalization scheduling rejection is tracked without an adapter "
        "callback");

  Flow rerun;
  rerun.service.Refresh(61);
  rerun.service.Refresh(61);
  rerun.scheduler.accept = false;
  rerun.scheduler.RunOne();
  Check(rerun.calls.size() == 1 && !rerun.calls.front().tokens.empty(),
        "the first coalesced refresh applies before internal rerun scheduling "
        "fails");
  rerun.service.FinalizeDue(0);
  const auto failedRerun = rerun.ActorRecord(61);
  Check(failedRerun && failedRerun->phase == ApplicationPhase::kFailed &&
            !failedRerun->problem.empty(),
        "rejected coalesced rerun becomes a visible application failure");
  if (!rerun.calls.empty()) {
    rerun.Render(rerun.calls.front());
  }
  const auto stillFailed = rerun.ActorRecord(61);
  Check(stillFailed && stillFailed->phase == ApplicationPhase::kFailed,
        "the earlier refresh cannot hide an internal rerun scheduling failure");

  Flow load;
  load.scheduler.accept = false;
  load.service.Refresh(71);
  load.service.BeginLoad();
  load.service.Resume();
  load.service.FinalizeDue(0);
  Check(
      !load.ActorRecord(71),
      "load discards an unprocessed scheduling rejection from the old session");
}

void RetirementBeforeRender() {
  Flow flow;
  (void)flow.service.Begin("saved", {42, 43});
  flow.service.Refresh(42);
  flow.service.Refresh(43);
  flow.scheduler.Drain();
  const Applied retired = flow.calls.front();
  const auto held = flow.service.Snapshot();
  flow.service.Retire(42, retired.tokens);
  const auto actor = flow.ActorRecord(42);
  Check(actor && actor->phase == ApplicationPhase::kUnmatched &&
            actor->problem ==
                "the actor was retired before rendering completed" &&
            flow.service.PendingFor(42).empty(),
        "eviction after preparation terminates the actor and recipe render "
        "waits");
  Check(!flow.service.PendingFor(43).empty() &&
            flow.ActorRecord(43)->phase == ApplicationPhase::kPrepared,
        "retiring one wearer preserves another wearer's pending application");
  flow.Render(retired);
  flow.service.Retire(42, retired.tokens);
  Check(flow.ActorRecord(42)->phase == ApplicationPhase::kUnmatched,
        "late render and duplicate retirement cannot revive a retired attempt");
  flow.Render(flow.calls.back());
  Check(flow.RecipeRecord("saved")->phase == ApplicationPhase::kUnmatched &&
            flow.service.PendingFor(43).empty(),
        "another wearer's success cannot hide the retired wearer's outcome");
  Check(std::ranges::all_of(held,
                            [](const ApplicationRecord &a_record) {
                              return a_record.phase ==
                                     ApplicationPhase::kPrepared;
                            }),
        "retirement leaves previously published snapshots intact");
  flow.service.Refresh(42);
  flow.scheduler.Drain();
  flow.service.Retire(42, retired.tokens);
  Check(flow.ActorRecord(42)->phase == ApplicationPhase::kPrepared,
        "returning from eviction creates an attempt immune to old retirement");
  flow.Render(flow.calls.back());
  Check(flow.ActorRecord(42)->phase == ApplicationPhase::kRendered,
        "an actor returning from eviction can render successfully");
}

void RetirementDuringReplacement() {
  Flow flow;
  (void)flow.service.Begin("saved", {42});
  (void)flow.service.Begin("paint", {42});
  flow.service.Refresh(42);
  flow.scheduler.Drain();
  const Applied retired = flow.calls.back();
  const auto replacement = flow.service.Begin("saved", {42});
  const auto pending = flow.service.PendingFor(42);
  flow.service.Retire(42, retired.tokens);
  Check(flow.service.PendingFor(42) == pending &&
            flow.RecipeRecord("saved")->token == replacement &&
            flow.RecipeRecord("paint")->phase == ApplicationPhase::kQueued,
        "retiring old live state preserves replacement and overlapping recipe "
        "work");
  flow.service.Refresh(42);
  flow.scheduler.Drain();
  flow.service.Retire(42, retired.tokens);
  Check(flow.RecipeRecord("saved")->phase == ApplicationPhase::kPrepared &&
            flow.RecipeRecord("paint")->phase == ApplicationPhase::kPrepared,
        "old retirement cannot terminate the replacement's prepared attempts");
  flow.Render(flow.calls.back());
  flow.service.Retire(42, flow.calls.back().tokens);
  Check(
      flow.RecipeRecord("saved")->phase == ApplicationPhase::kRendered &&
          flow.RecipeRecord("paint")->phase == ApplicationPhase::kRendered,
      "retiring successfully rendered state preserves its completed outcomes");
}

void RetirementAcrossLoad() {
  Flow flow;
  (void)flow.service.Begin("saved", {42});
  flow.service.Refresh(42);
  flow.scheduler.Drain();
  const Applied retired = flow.calls.back();
  flow.service.BeginLoad();
  flow.service.Retire(42, retired.tokens);
  Check(flow.RecipeRecord("saved")->phase == ApplicationPhase::kCancelled,
        "retirement during load preserves cancellation");
  flow.service.Resume();
  (void)flow.service.Begin("saved", {42});
  flow.service.Refresh(42);
  flow.scheduler.Drain();
  const auto pending = flow.service.PendingFor(42);
  flow.service.Retire(42, retired.tokens);
  Check(flow.service.PendingFor(42) == pending && !pending.empty(),
        "old-session retirement cannot terminate reused actor and recipe "
        "identities");
}

void SchedulingFailureAndRetry() {
  Flow flow;
  flow.scheduler.accept = false;
  Check(!flow.service.Refresh(90),
        "rejected scheduler submission is returned to its caller");
  flow.service.FinalizeDue(0);
  const auto failed = flow.ActorRecord(90);
  Check(failed && failed->phase == ApplicationPhase::kFailed &&
            !failed->problem.empty(),
        "rejected ordinary refresh produces a visible tracked failure");
  flow.scheduler.accept = true;
  Check(flow.service.Refresh(90),
        "failed scheduling leaves the actor retryable");
  flow.scheduler.Drain();
  const auto retry = flow.ActorRecord(90);
  Check(failed && retry && retry->token.revision > failed->token.revision &&
            retry->phase == ApplicationPhase::kPrepared &&
            flow.calls.size() == 1,
        "retry gets a new tracked revision without a phantom duplicate");
  if (!flow.calls.empty()) {
    flow.Render(flow.calls.back());
  }
  const auto rendered = flow.ActorRecord(90);
  Check(rendered && rendered->phase == ApplicationPhase::kRendered,
        "an ordinary refresh recovers from rejected scheduling");
}
}

int main() {
  OrdinaryRefreshAndFirstRender();
  EquipFinalize();
  CoalescedRerunAndStaleActorResult();
  IndependentActorsAndRecipeOverlap();
  LoadCancellationAndLoadedRefresh();
  SchedulingFailureAndRetry();
  RejectedInternalScheduling();
  RetirementBeforeRender();
  RetirementDuringReplacement();
  RetirementAcrossLoad();
  return test::Finish("engine_applicator");
}
