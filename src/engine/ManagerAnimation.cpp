// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "diagnostics/Trace.h"
#include "engine/GameObjectService.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects {
void Manager::RetireAll() {
  const Trace::Scope trace{Trace::Command("manager.RetireAll")};
  PostTask([this] {
    std::vector<RE::FormID> ids = animations_.Actors();
    for (const auto &[actor, state] : applied_) {
      ids.push_back(actor);
    }
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const RE::FormID actor : ids) {
      QueueRetire(actor);
    }
  });
}

void Manager::QueueAnimationEvent(AnimationEvent a_event) {
  if (!a_event.Current()) {
    return;
  }
  PostTask([this, event = std::move(a_event)] {
    if (!event.Current()) {
      return;
    }
    const RE::FormID id = event.registration->actor;
    const auto found = applied_.find(id);
    if (found == applied_.end()) {
      return;
    }
    const auto actor = found->second.actor.get();
    if (!actor || !animations_.Accept(event, *actor)) {
      return;
    }
    NoteAnimEvent(id, event.tag);
    EventRecord record;
    record.id = "anim." + event.tag;
    record.payload.arg = event.payload;
    Fire(id, record);
  });
}

void Manager::ReconcileAnimationEvents(RE::FormID a_actor) {
  const auto found = applied_.find(a_actor);
  if (found != applied_.end()) {
    const auto actor = found->second.actor.get();
    if (actor && !actor->IsDeleted() && actor->Is3DLoaded()) {
      animations_.Reconcile(*actor);
      return;
    }
  }
  animations_.Stop(a_actor);
  ForgetAnimEvents(a_actor);
}

void Manager::SweepAnimationEvents() {
  for (const auto &[actor, state] : applied_) {
    ReconcileAnimationEvents(actor);
  }
  for (const RE::FormID actor : animations_.Actors()) {
    if (!applied_.contains(actor) && applications_.PendingFor(actor).empty()) {
      ReconcileAnimationEvents(actor);
    }
  }
}
}
