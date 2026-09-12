#include "engine/ApplicationService.h"
#include "engine/SessionQueue.h"
#include "test_support.h"

#include <algorithm>
#include <deque>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
ApplicationRecord Record(const ApplicationService &a_service,
                         std::string_view a_recipe) {
  const auto snapshot = a_service.Snapshot();
  const auto found =
      std::ranges::find_if(snapshot, [&](const ApplicationRecord &a_record) {
        return a_record.token.recipeID == a_recipe;
      });
  Check(found != snapshot.end(), "application record exists");
  return found == snapshot.end() ? ApplicationRecord{} : *found;
}

struct QueuedApplications {
  ApplicationService service;
  bool accept = true;
  bool retainedOldTexture = true;
  std::deque<SessionQueue::Task> tasks;
  std::map<std::uint32_t, std::vector<ApplicationToken>> live;
  SessionQueue queue{[this](SessionQueue::Task a_task) {
                       if (!accept) {
                         return false;
                       }
                       tasks.push_back(std::move(a_task));
                       return true;
                     },
                     [this](std::uint32_t a_actor) {
                       live[a_actor] = service.PendingFor(a_actor);
                       for (const ApplicationToken &token : live[a_actor]) {
                         service.Report(token, a_actor,
                                        ApplicationPhase::kPrepared);
                       }
                     }};

  QueuedApplications() { queue.Resume(); }

  ApplicationToken Apply(std::string a_recipe,
                         std::vector<std::uint32_t> a_candidates) {
    const auto token =
        service.Begin(std::move(a_recipe), std::move(a_candidates));
    for (const auto actor : service.ActorsFor(token.recipeID)) {
      live.erase(actor);
      if (!queue.Refresh(actor)) {
        service.Report(token, actor, ApplicationPhase::kFailed,
                       "queue refused");
      }
    }
    return token;
  }

  void Drain() {
    for (std::size_t i = 0; i < 32 && !tasks.empty(); ++i) {
      auto task = std::move(tasks.front());
      tasks.pop_front();
      task();
    }
    Check(tasks.empty(), "the fake scheduler drains bounded refresh work");
  }

  void Draw(std::uint32_t a_actor, bool a_success) {
    const auto found = live.find(a_actor);
    if (found == live.end()) {
      return;
    }
    for (const auto &token : found->second) {
      service.Report(token, a_actor,
                     a_success ? ApplicationPhase::kRendered
                               : ApplicationPhase::kFailed,
                     a_success ? "" : "draw refused");
    }
  }

  void Unload(std::uint32_t a_actor) {
    const auto found = live.find(a_actor);
    if (found == live.end()) {
      return;
    }
    for (const auto &token : found->second) {
      service.Report(token, a_actor, ApplicationPhase::kUnmatched,
                     "actor unloaded");
    }
    live.erase(found);
  }

  void Load() {
    queue.BeginLoad();
    service.Cancel();
    live.clear();
    queue.Resume();
  }
};

void QueuedRapidRevisions() {
  QueuedApplications flow;
  flow.live[10] = {};
  const auto first = flow.Apply("saved", {10});
  Check(!flow.live.contains(10),
        "application retires an actor before queuing refresh");
  const auto saved = flow.Apply("saved", {});
  const auto scratch = flow.Apply("paint", {10});
  Check(flow.tasks.size() == 1,
        "rapid saved and scratch edits share the real queue's pending actor");
  flow.Drain();
  Check(Record(flow.service, "saved").phase == ApplicationPhase::kPrepared &&
            Record(flow.service, "paint").phase == ApplicationPhase::kPrepared,
        "refresh captures latest saved and scratch revisions after retirement");
  Check(flow.live.at(10) == std::vector<ApplicationToken>{scratch, saved},
        "the adapter stamps live state with tokens captured at refresh");
  const auto stale = flow.live.at(10);
  const auto newer = flow.Apply("saved", {});
  for (const auto &token : stale) {
    flow.service.Report(token, 10, ApplicationPhase::kRendered);
  }
  flow.service.Report(first, 10, ApplicationPhase::kRendered);
  Check(
      flow.service.PendingFor(10) == std::vector<ApplicationToken>{newer},
      "old draw completion cannot acknowledge the queued replacement revision");
  flow.Drain();
  flow.Draw(10, true);
  Check(Record(flow.service, "saved").phase == ApplicationPhase::kRendered,
        "the replacement reaches rendered only after refresh and draw");
}

void QueuedFailuresAndRetry() {
  QueuedApplications flow;
  flow.accept = false;
  const auto refused = flow.Apply("saved", {20});
  Check(flow.tasks.empty() &&
            Record(flow.service, "saved").phase == ApplicationPhase::kFailed &&
            Record(flow.service, "saved").problem == "queue refused",
        "real queue submission refusal becomes a terminal application failure");
  flow.accept = true;
  const auto retry = flow.Apply("saved", {});
  flow.Drain();
  Check(retry.revision > refused.revision && flow.live.contains(20),
        "retry after submission refusal successfully prepares the same actor");
  flow.Draw(20, false);
  Check(flow.retainedOldTexture &&
            Record(flow.service, "saved").phase == ApplicationPhase::kFailed &&
            Record(flow.service, "saved").problem == "draw refused",
        "failed draw remains failed even though an old texture is retained");
  (void)flow.Apply("saved", {});
  flow.Drain();
  flow.Draw(20, true);
  Check(Record(flow.service, "saved").phase == ApplicationPhase::kRendered,
        "explicit retry can recover a failed first draw");
}

void QueuedCancellationAndUnload() {
  QueuedApplications flow;
  const auto cancelled = flow.Apply("paint", {30});
  flow.Load();
  const auto current = flow.Apply("paint", {31});
  flow.Drain();
  Check(!flow.live.contains(30) && flow.live.contains(31),
        "load invalidates queued callbacks before a new session refresh");
  flow.service.Report(cancelled, 30, ApplicationPhase::kRendered);
  Check(flow.service.PendingFor(31) == std::vector<ApplicationToken>{current},
        "cancelled session render cannot acknowledge new session work");
  flow.Unload(31);
  Check(Record(flow.service, "paint").phase == ApplicationPhase::kUnmatched &&
            flow.service.PendingFor(31).empty(),
        "unloading after preparation terminates the first-render wait");
}

bool HasActor(const std::vector<ApplicationRecord> &a_records,
              std::uint32_t a_actorID) {
  return std::ranges::any_of(a_records, [&](const ApplicationRecord &a_record) {
    return a_record.token.actorID == a_actorID;
  });
}

void LifecycleRetention() {
  ApplicationService service{
      [](SessionQueue::Task a_task) {
        a_task();
        return true;
      },
      [&](std::uint32_t a_actorID,
          const std::vector<ApplicationToken> &a_tokens) {
        for (const auto &token : a_tokens) {
          service.Report(token, a_actorID, ApplicationPhase::kPrepared);
        }
      }};
  service.Resume();
  for (std::uint32_t actor = 1; actor <= 600; ++actor) {
    (void)service.Refresh(actor);
    if (actor <= 300) {
      continue;
    }
    for (const auto &token : service.PendingFor(actor)) {
      service.Report(token, actor, ApplicationPhase::kRendered);
    }
  }
  const auto retained = service.Snapshot();
  Check(retained.size() == 300 + kMaxTerminalApplicationActors,
        "terminal actor retention is capped without evicting pending actors");
  Check(HasActor(retained, 1) && HasActor(retained, 300) &&
            !HasActor(retained, 301) && HasActor(retained, 345) &&
            HasActor(retained, 600),
        "retention preserves pending actors and newest terminal revisions");
  const auto scoped = service.Begin("saved", {1, 600});
  Check(service.PendingFor(1) == std::vector<ApplicationToken>{scoped},
        "recipe supersession prunes terminal actors without losing new scoped "
        "work");
  Check(service.Snapshot().size() == 299 + kMaxTerminalApplicationActors + 1,
        "recipe supersession applies the same terminal retention bound");
  service.BeginLoad();
  const auto cancelled = service.Snapshot();
  Check(cancelled.size() == kMaxTerminalApplicationActors + 1 &&
            std::ranges::all_of(cancelled,
                                [](const ApplicationRecord &a_record) {
                                  return a_record.phase ==
                                         ApplicationPhase::kCancelled;
                                }),
        "load publishes bounded terminal cancellation records before resuming");
  service.Resume();
  Check(service.Snapshot().size() == 1 &&
            service.Snapshot().front().token.actorID == 0,
        "resume releases cancelled lifecycle records from the previous load");
  Check(cancelled.size() == kMaxTerminalApplicationActors + 1,
        "held cancellation snapshots survive lifecycle record pruning");
  (void)service.Refresh(700);
  Check(service.PendingFor(700).size() == 1 && service.PendingFor(1).empty(),
        "post-load refresh tracks only fresh pending lifecycle work");
}

void RapidRevisions() {
  ApplicationService service;
  const auto old = service.Begin("saved", {3, 2, 3, 0});
  const auto held = service.Snapshot();
  Check(service.ActorsFor("saved") == std::vector<std::uint32_t>{2, 3},
        "target actor IDs are unique, sorted, and nonzero");
  const auto latest = service.Begin("saved", {4});
  Check(latest.revision > old.revision && service.Snapshot().size() == 1,
        "a recipe retains only its latest application revision");
  Check(service.ActorsFor("saved") == std::vector<std::uint32_t>{2, 3, 4},
        "rapid edits retain actors retired by the earlier application");
  service.Report(old, 2, ApplicationPhase::kRendered);
  Check(Record(service, "saved").phase == ApplicationPhase::kQueued,
        "old refresh results cannot complete a newer revision");
  Check(service.PendingFor(2) == std::vector<ApplicationToken>{latest},
        "coalesced actor refresh captures the latest revision token");
  service.Report(latest, 99, ApplicationPhase::kFailed, "foreign actor");
  Check(Record(service, "saved").problem.empty(),
        "a foreign actor cannot corrupt an application");
  for (const auto actor : {2u, 3u, 4u}) {
    service.Report(latest, actor, ApplicationPhase::kPrepared);
  }
  Check(Record(service, "saved").phase == ApplicationPhase::kPrepared,
        "preparation is acknowledged separately from rendering");
  service.Report(latest, 2, ApplicationPhase::kRendered);
  Check(Record(service, "saved").phase == ApplicationPhase::kPrepared,
        "one rendered actor cannot complete other actors");
  service.Report(latest, 3, ApplicationPhase::kRendered);
  service.Report(latest, 4, ApplicationPhase::kRendered);
  Check(Record(service, "saved").phase == ApplicationPhase::kRendered &&
            service.PendingFor(2).empty(),
        "all targets must render before aggregate success");
  service.Report(latest, 2, ApplicationPhase::kPrepared);
  Check(Record(service, "saved").phase == ApplicationPhase::kRendered,
        "late preparation cannot regress a completed first render");
  Check(held.front().token == old &&
            held.front().phase == ApplicationPhase::kQueued,
        "previously published snapshots remain independent values");
}

void FailuresAndCancellation() {
  ApplicationService service;
  const auto attempt = service.Begin("paint", {5, 6});
  service.Report(attempt, 5, ApplicationPhase::kFailed,
                 "texture lab unavailable");
  Check(Record(service, "paint").phase == ApplicationPhase::kFailed &&
            Record(service, "paint").problem == "texture lab unavailable",
        "preparation or rendering failure is visible with its reason");
  Check(service.PendingFor(6) == std::vector<ApplicationToken>{attempt},
        "other actors can finish after an individual failure");
  service.Report(attempt, 5, ApplicationPhase::kRendered);
  Check(Record(service, "paint").phase == ApplicationPhase::kFailed,
        "late success cannot erase a terminal failure");
  const auto retry = service.Begin("paint", {});
  Check(service.ActorsFor("paint") == std::vector<std::uint32_t>{5, 6},
        "retry keeps failed and pending actor targets");
  service.Report(attempt, 6, ApplicationPhase::kRendered);
  Check(service.PendingFor(6) == std::vector<ApplicationToken>{retry},
        "an earlier attempt cannot acknowledge a retry");
  service.Cancel();
  Check(Record(service, "paint").phase == ApplicationPhase::kCancelled &&
            service.PendingFor(6).empty() && service.ActorsFor("paint").empty(),
        "load cancellation terminates pending work and clears active targets");
  service.Report(retry, 5, ApplicationPhase::kRendered);
  Check(Record(service, "paint").phase == ApplicationPhase::kCancelled,
        "pre-load reports cannot revive cancelled work");
  const auto restarted = service.Begin("paint", {7});
  Check(restarted.revision > retry.revision &&
            service.ActorsFor("paint") == std::vector<std::uint32_t>{7},
        "a new load uses fresh targets and globally newer revision tokens");
}

void UnmatchedAndSharedRoute() {
  ApplicationService service;
  const auto empty = service.Begin("saved", {});
  Check(Record(service, "saved").phase == ApplicationPhase::kUnmatched,
        "no wearers is explicit unmatched rather than render success");
  service.Report(empty, 1, ApplicationPhase::kRendered);
  Check(Record(service, "saved").phase == ApplicationPhase::kUnmatched,
        "an unregistered actor cannot turn an empty application into success");
  const auto saved = service.Begin("saved", {1, 2});
  const auto scratch = service.Begin("paint", {1});
  Check(service.PendingFor(1) == std::vector<ApplicationToken>{scratch, saved},
        "saved recipes and scratch previews share the actor application route");
  service.Report(saved, 1, ApplicationPhase::kRendered);
  service.Report(saved, 2, ApplicationPhase::kUnmatched, "actor unloaded");
  Check(Record(service, "saved").phase == ApplicationPhase::kUnmatched &&
            Record(service, "saved").problem == "actor unloaded",
        "a missing target is not reported as all-target render success");
  service.Report(scratch, 1, ApplicationPhase::kFailed, "mask render failed");
  Check(Record(service, "paint").problem == "mask render failed",
        "scratch failures use the same application outcome record");
}

void WholeCatalogOverlap() {
  ApplicationService service;
  const auto saved = service.Begin("saved", {1});
  const auto paint = service.Begin("paint", {2});
  const auto all = service.Begin("", {3});
  Check(service.ActorsFor("") == std::vector<std::uint32_t>{1, 2, 3},
        "whole-catalog apply inherits actors from recipe-scoped pending work");
  Check(Record(service, "saved").phase == ApplicationPhase::kCancelled &&
            Record(service, "paint").phase == ApplicationPhase::kCancelled,
        "whole-catalog apply supersedes old recipe outcomes");
  service.Report(saved, 1, ApplicationPhase::kRendered);
  service.Report(paint, 2, ApplicationPhase::kRendered);
  Check(service.PendingFor(1) == std::vector<ApplicationToken>{all},
        "old scoped render cannot acknowledge the whole-catalog revision");
  service.Report(all, 1, ApplicationPhase::kPrepared);
  const auto next = service.Begin("saved", {4});
  Check(service.ActorsFor("saved") == std::vector<std::uint32_t>{1, 2, 3, 4},
        "a newer recipe edit conservatively inherits whole-catalog targets");
  Check(Record(service, "").phase == ApplicationPhase::kCancelled,
        "a recipe edit invalidates older whole-catalog render results");
  service.Report(all, 1, ApplicationPhase::kRendered);
  Check(service.PendingFor(1) == std::vector<ApplicationToken>{next},
        "stale whole-catalog results cannot complete the newer recipe edit");
  const auto again = service.Begin("", {});
  Check(service.ActorsFor("") == std::vector<std::uint32_t>{1, 2, 3, 4} &&
            again.revision > next.revision,
        "alternating scope changes preserve the entire pending actor set");
}
}

int main() {
  RapidRevisions();
  FailuresAndCancellation();
  UnmatchedAndSharedRoute();
  WholeCatalogOverlap();
  QueuedRapidRevisions();
  QueuedFailuresAndRetry();
  QueuedCancellationAndUnload();
  LifecycleRetention();
  Check(ApplicationPhaseName(ApplicationPhase::kRendered) == "rendered" &&
            ApplicationPhaseName(static_cast<ApplicationPhase>(99)) ==
                "unknown",
        "phase labels are safe for known and unknown values");
  return test::Finish("application service");
}
