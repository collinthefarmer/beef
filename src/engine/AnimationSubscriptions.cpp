// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/AnimationSubscriptions.h"

#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"

#include <utility>

namespace BetterEnchantmentEffects {
namespace {
RE::BSTSmartPointer<RE::BShkbAnimationGraph>
AnimationGraphOf(RE::Actor &a_actor) {
  if (a_actor.IsDeleted() || !a_actor.Is3DLoaded()) {
    return {};
  }
  RE::BSAnimationGraphManagerPtr manager;
  if (!a_actor.GetAnimationGraphManager(manager) || !manager) {
    return {};
  }
  for (const auto &graph : manager->graphs) {
    if (graph) {
      return graph;
    }
  }
  return {};
}
}

bool AnimationEvent::Current() const noexcept {
  return registration && registration->active.load();
}

AnimationSubscriptions::AnimationSubscriptions(Submit a_submit)
    : submit_(std::move(a_submit)) {}

// NOLINTNEXTLINE(bugprone-exception-escape)
AnimationSubscriptions::~AnimationSubscriptions() { Clear(); }

void AnimationSubscriptions::Reconcile(RE::Actor &a_actor) {
  const RE::FormID id = a_actor.GetFormID();
  auto graph = AnimationGraphOf(a_actor);
  const auto existing = subscriptions_.find(id);
  if (existing != subscriptions_.end() &&
      existing->second.actor.get().get() == &a_actor &&
      existing->second.graph.get() == graph.get()) {
    return;
  }
  Stop(id);
  if (id == 0) {
    return;
  }
  auto registration = std::make_shared<AnimationRegistration>();
  registration->actor = id;
  if (!graph) {
    registration->active.store(false);
    subscriptions_.emplace(id,
                           Subscription{a_actor.GetHandle(), {}, registration});
    return;
  }
  Source *source = graph->GetEventSource<RE::BSAnimationGraphEvent>();
  subscriptions_.emplace(
      id, Subscription{a_actor.GetHandle(), std::move(graph), registration});
  try {
    bool inserted = false;
    {
      const std::scoped_lock lock{sourceLock_};
      inserted = sources_.emplace(source, registration).second;
    }
    if (!inserted) {
      registration->active.store(false);
      subscriptions_.erase(id);
      return;
    }
    source->AddEventSink(this);
    Metrics::CountSinkAdd();
  } catch (...) {
    Stop(id);
    throw;
  }
}

void AnimationSubscriptions::Stop(RE::FormID a_actor) {
  const auto found = subscriptions_.find(a_actor);
  if (found == subscriptions_.end()) {
    return;
  }
  Subscription subscription = std::move(found->second);
  subscriptions_.erase(found);
  subscription.registration->active.store(false);
  if (!subscription.graph) {
    return;
  }
  Source *source =
      subscription.graph->GetEventSource<RE::BSAnimationGraphEvent>();
  {
    const std::scoped_lock lock{sourceLock_};
    sources_.erase(source);
  }
  source->RemoveEventSink(this);
  Metrics::CountSinkRemove();
}

void AnimationSubscriptions::Clear() {
  while (!subscriptions_.empty()) {
    Stop(subscriptions_.begin()->first);
  }
}

std::vector<RE::FormID> AnimationSubscriptions::Actors() const {
  std::vector<RE::FormID> actors;
  actors.reserve(subscriptions_.size());
  for (const auto &[actor, subscription] : subscriptions_) {
    actors.push_back(actor);
  }
  return actors;
}

bool AnimationSubscriptions::Accept(const AnimationEvent &a_event,
                                    RE::Actor &a_actor) {
  if (!a_event.Current() ||
      a_event.registration->actor != a_actor.GetFormID()) {
    return false;
  }
  Reconcile(a_actor);
  const auto found = subscriptions_.find(a_actor.GetFormID());
  return a_event.Current() && found != subscriptions_.end() &&
         found->second.registration == a_event.registration;
}

RE::BSEventNotifyControl
AnimationSubscriptions::ProcessEvent(const RE::BSAnimationGraphEvent *a_event,
                                     Source *a_source) {
  if (!a_event || !a_event->holder || !a_event->tag.c_str() ||
      !*a_event->tag.c_str()) {
    return RE::BSEventNotifyControl::kContinue;
  }
  try {
    std::shared_ptr<const AnimationRegistration> registration;
    {
      const std::scoped_lock lock{sourceLock_};
      const auto found = sources_.find(a_source);
      if (found == sources_.end()) {
        return RE::BSEventNotifyControl::kContinue;
      }
      registration = found->second;
    }
    if (!registration->active.load() ||
        a_event->holder->GetFormID() != registration->actor || !submit_) {
      return RE::BSEventNotifyControl::kContinue;
    }
    AnimationEvent event{registration, a_event->tag.c_str(),
                         a_event->payload.c_str() ? a_event->payload.c_str()
                                                  : ""};
    submit_(std::move(event));
  } catch (...) {
    Trace::Emit(Trace::Event::kCaptureFailure, {});
  }
  return RE::BSEventNotifyControl::kContinue;
}
}
