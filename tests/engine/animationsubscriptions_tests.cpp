// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Trace.h"
#include "engine/GameObjectService.h"
#include "engine/Manager.h"
#include "test_support.h"

#include <condition_variable>
#include <stdexcept>
#include <thread>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void Ownership() {
  const auto actor = std::make_shared<RE::Actor>();
  const auto first = std::make_shared<RE::BShkbAnimationGraph>();
  const auto second = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {nullptr, first, second};
  std::vector<AnimationEvent> events;
  AnimationSubscriptions owner{
      [&](AnimationEvent a_event) { events.push_back(std::move(a_event)); }};
  const RE::BSAnimationGraphEvent event{{"footstep"}, {"left"}, actor.get()};
  first->adding = [&](auto *a_sink) {
    a_sink->ProcessEvent(&event, first.get());
  };
  first->removing = [&](auto *a_sink) {
    a_sink->ProcessEvent(&event, first.get());
  };
  owner.Reconcile(*actor);
  owner.Reconcile(*actor);
  Check(first->adds == 1 && second->adds == 0 && events.size() == 1,
        "first nonnull graph is attached once; add can call back without "
        "deadlock");
  first->Send(event);
  Check(events.size() == 2 && events.back().tag == "footstep" &&
            events.back().payload == "left" &&
            owner.Accept(events.back(), *actor),
        "captured event owns its strings and accepts the current actor and "
        "graph");
  const auto stale = events.back();
  actor->manager->graphs = {second};
  Check(
      !owner.Accept(stale, *actor) && first->removes == 1 &&
          second->adds == 1 && events.size() == 2,
      "delivery detects graph replacement and removal callbacks are rejected");
  second->Send(event);
  const auto replacementEvent = events.back();
  const auto replacementActor = std::make_shared<RE::Actor>();
  replacementActor->manager->graphs = {second};
  Check(!owner.Accept(replacementEvent, *replacementActor) &&
            second->removes == 1 && second->adds == 2,
        "a reused form ID with a different actor invalidates the registration");
  owner.Clear();
  owner.Clear();
  Check(owner.Actors().empty() && second->removes == 2,
        "clear detaches each exact source once");

  auto retained = std::make_shared<RE::BShkbAnimationGraph>();
  const std::weak_ptr weak = retained;
  actor->manager->graphs = {retained};
  owner.Reconcile(*actor);
  actor->manager.reset();
  retained.reset();
  Check(!weak.expired(), "subscription retains a replaced graph until detach");
  owner.Stop(actor->id);
  Check(weak.expired(),
        "stop releases the graph even without a current manager");
}

void QueuedDelivery() {
  observedTags.clear();
  Manager manager;
  const auto actor = std::make_shared<RE::Actor>();
  const auto graph = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {graph};
  manager.applied_[actor->id] = {actor->GetHandle()};
  manager.ReconcileAnimationEvents(actor->id);
  const RE::BSAnimationGraphEvent event{{"footstep"}, {"left"}, actor.get()};
  Trace::Get().Enable(false);
  graph->Send(event);
  Check(observedTags.empty() && manager.fired.empty(),
        "animation callbacks only queue work, even with tracing disabled");
  manager.Drain();
  Trace::Get().Enable(true);
  Check(observedTags[actor->id].contains("footstep") &&
            manager.fired.size() == 1 &&
            manager.fired.back().id == "anim.footstep" &&
            manager.fired.back().payload.arg == "left",
        "accepted work updates discovery and signals together independently of "
        "tracing");
  manager.applied_.clear();
  const auto pending = manager.applications_.Begin("recipe", {actor->id});
  manager.SweepAnimationEvents();
  graph->Send(event);
  manager.Drain();
  Check(graph->removes == 0 && manager.fired.size() == 1 &&
            observedTags[actor->id].contains("footstep"),
        "pending rebuild retains observation but events do not fire into "
        "absent effects");
  manager.applied_[actor->id] = {actor->GetHandle()};
  manager.ReconcileAnimationEvents(actor->id);
  Check(graph->adds == 1 && graph->removes == 0,
        "restored effect state reuses an unchanged subscription");
  graph->Send(event);
  manager.animations_.Stop(actor->id);
  ForgetAnimEvents(actor->id);
  manager.ReconcileAnimationEvents(actor->id);
  manager.Drain();
  Check(observedTags.empty() && manager.fired.size() == 1,
        "queued events cannot cross a stop and resubscribe boundary");
  graph->Send(event);
  manager.applications_.BeginLoad();
  manager.animations_.Clear();
  observedTags.clear();
  manager.applications_.Resume();
  manager.ReconcileAnimationEvents(actor->id);
  manager.Drain();
  Check(observedTags.empty() && manager.fired.size() == 1,
        "old session tasks cannot repopulate discovery or fire after resume");
  manager.accepting = false;
  graph->Send(event);
  manager.Drain();
  Check(observedTags.empty(),
        "rejected task submission does not discover an event");
  manager.applied_.clear();
  manager.applications_.Report(pending, actor->id, ApplicationPhase::kFailed);
  manager.SweepAnimationEvents();
  Check(manager.animations_.Actors().empty(),
        "maintenance ends observation after a rebuild no longer has pending "
        "work");
}

void MissingGraphs() {
  observedTags.clear();
  Manager manager;
  const auto actor = std::make_shared<RE::Actor>();
  manager.applied_[actor->id] = {actor->GetHandle()};
  manager.SweepAnimationEvents();
  Check(manager.animations_.Actors().size() == 1,
        "an actor without a graph remains tracked for recovery and cleanup");
  const auto graph = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {nullptr, graph};
  manager.SweepAnimationEvents();
  Check(graph->adds == 1,
        "maintenance attaches when a graph becomes available");
  const RE::BSAnimationGraphEvent event{{"ready"}, {}, actor.get()};
  graph->Send(event);
  manager.Drain();
  Check(observedTags[actor->id].contains("ready") &&
            manager.fired.back().payload.arg.empty(),
        "null payload becomes an empty argument");
  actor->manager->graphs.clear();
  manager.SweepAnimationEvents();
  Check(graph->removes == 1 && observedTags[actor->id].contains("ready"),
        "temporary graph loss detaches but retains discovery during "
        "participation");
  manager.applied_.clear();
  const auto pending = manager.applications_.Begin("recipe", {actor->id});
  manager.SweepAnimationEvents();
  Check(!observedTags.empty(),
        "pending rebuild preserves missing-graph discovery");
  manager.applications_.Report(pending, actor->id, ApplicationPhase::kFailed);
  manager.SweepAnimationEvents();
  Check(observedTags.empty() && manager.animations_.Actors().empty(),
        "failed rebuild clears discovery even when no graph remains");
  actor->manager->graphs = {graph};
  manager.applied_[actor->id] = {actor->GetHandle()};
  manager.SweepAnimationEvents();
  actor->loaded = false;
  manager.SweepAnimationEvents();
  Check(graph->removes == 2 && manager.animations_.Actors().empty(),
        "unloaded actors end observation");
  actor->loaded = true;
  manager.SweepAnimationEvents();
  actor->deleted = true;
  manager.SweepAnimationEvents();
  Check(graph->removes == 3 && manager.animations_.Actors().empty(),
        "deleted actors end observation");
}

void InFlightCallback() {
  observedTags.clear();
  Manager manager;
  const auto actor = std::make_shared<RE::Actor>();
  const auto graph = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {graph};
  manager.applied_[actor->id] = {actor->GetHandle()};
  std::mutex lock;
  std::condition_variable condition;
  bool captured = false;
  bool resume = false;
  AnimationSubscriptions owner{[&](AnimationEvent a_event) {
    {
      std::unique_lock guard{lock};
      captured = true;
      condition.notify_one();
      condition.wait(guard, [&] { return resume; });
    }
    manager.QueueAnimationEvent(std::move(a_event));
  }};
  owner.Reconcile(*actor);
  const RE::BSAnimationGraphEvent event{{"stale"}, {}, actor.get()};
  std::thread callback{[&] { owner.ProcessEvent(&event, graph.get()); }};
  {
    std::unique_lock guard{lock};
    condition.wait(guard, [&] { return captured; });
  }
  manager.applications_.BeginLoad();
  owner.Clear();
  manager.applications_.Resume();
  manager.ReconcileAnimationEvents(actor->id);
  {
    const std::scoped_lock guard{lock};
    resume = true;
  }
  condition.notify_one();
  callback.join();
  Check(manager.tasks.empty(),
        "a stale captured callback does not submit work after resume");
  manager.Drain();
  Check(
      manager.tasks.empty() && manager.fired.empty() && observedTags.empty(),
      "a callback captured before clear cannot post into the resumed session");
}

void RetirementOrdering() {
  observedTags.clear();
  Manager manager;
  const auto actor = std::make_shared<RE::Actor>();
  const auto graph = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {graph};
  manager.applied_[actor->id] = {actor->GetHandle()};
  manager.ReconcileAnimationEvents(actor->id);
  NoteAnimEvent(actor->id, "ready");
  manager.PostTask([&] {
    manager.applied_.clear();
    manager.PostTask([&] {
      manager.applied_[actor->id] = {actor->GetHandle()};
      manager.ReconcileAnimationEvents(actor->id);
    });
  });
  manager.RetireAll();
  manager.Drain();
  Check(manager.applied_.empty() && manager.animations_.Actors().empty() &&
            observedTags.empty() && graph->adds == 1 && graph->removes == 1,
        "retire all includes actors in rebuild gaps and runs after their "
        "queued refreshes");
}

void InvalidEvents() {
  const auto actor = std::make_shared<RE::Actor>();
  const auto graph = std::make_shared<RE::BShkbAnimationGraph>();
  actor->manager->graphs = {graph};
  int submitted = 0;
  AnimationSubscriptions owner{[&](AnimationEvent) { ++submitted; }};
  owner.Reconcile(*actor);
  RE::BSAnimationGraphEvent event{{"tag"}, {}, actor.get()};
  owner.ProcessEvent(nullptr, graph.get());
  owner.ProcessEvent(&event, nullptr);
  event.holder = nullptr;
  graph->Send(event);
  event.holder = actor.get();
  event.tag.value = nullptr;
  graph->Send(event);
  event.tag.value = "";
  graph->Send(event);
  event.tag.value = "tag";
  const auto other = std::make_shared<RE::Actor>();
  other->id = 2;
  event.holder = other.get();
  graph->Send(event);
  Check(submitted == 0,
        "malformed events, unknown sources and mismatched holders are ignored");
  owner.Clear();
  AnimationSubscriptions throwing{
      [](AnimationEvent) { throw std::runtime_error("rejected"); }};
  throwing.Reconcile(*actor);
  event.holder = actor.get();
  graph->Send(event);
  Check(throwing.Actors().size() == 1,
        "submission exceptions do not escape the engine callback");
}
}

int main() {
  Ownership();
  QueuedDelivery();
  MissingGraphs();
  InFlightCallback();
  RetirementOrdering();
  InvalidEvents();
  return test::Finish("engine_animationsubscriptions");
}
