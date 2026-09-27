// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/AnimationSubscriptions.h"
#include "engine/ApplicationService.h"
#include "engine/GameObjectService.h"
#include "recipe/Signals.h"

#include <deque>
#include <utility>

namespace BetterEnchantmentEffects {
struct Manager {
  struct LiveActor {
    RE::ActorHandle actor;
  };
  std::deque<SessionQueue::Task> tasks;
  bool accepting = true;
  ApplicationService applications_{
      [this](SessionQueue::Task a_task) {
        if (!accepting)
          return false;
        tasks.push_back(std::move(a_task));
        return true;
      },
      [](std::uint32_t, const std::vector<ApplicationToken> &) {}};
  AnimationSubscriptions animations_{[this](AnimationEvent a_event) {
    QueueAnimationEvent(std::move(a_event));
  }};
  std::unordered_map<RE::FormID, LiveActor> applied_;
  std::vector<EventRecord> fired;

  Manager() { applications_.Resume(); }
  void PostTask(SessionQueue::Task a_task) {
    applications_.Post(std::move(a_task));
  }
  void Fire(RE::FormID, const EventRecord &a_event) {
    fired.push_back(a_event);
  }
  void QueueRetire(RE::FormID a_actor) {
    PostTask([this, a_actor] {
      applications_.Retire(a_actor, applications_.PendingFor(a_actor));
      animations_.Stop(a_actor);
      ForgetAnimEvents(a_actor);
      applied_.erase(a_actor);
    });
  }
  void Drain() {
    while (!tasks.empty()) {
      auto task = std::move(tasks.front());
      tasks.pop_front();
      task();
    }
  }
  void QueueAnimationEvent(AnimationEvent a_event);
  void ReconcileAnimationEvents(RE::FormID a_actor);
  void SweepAnimationEvents();
  void RetireAll();
};
}
