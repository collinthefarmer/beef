#include "engine/SessionQueue.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
struct SessionQueue::State {
  State(Submit a_submit, RefreshActor a_refresh, RefreshRejected a_rejected)
      : submit(std::move(a_submit)), refresh(std::move(a_refresh)),
        rejected(std::move(a_rejected)) {}

  [[nodiscard]] bool Current(std::uint64_t a_generation) const {
    return !loading && generation == a_generation;
  }

  std::mutex lock;
  Submit submit;
  RefreshActor refresh;
  RefreshRejected rejected;
  std::uint64_t generation = 0;
  bool loading = true;
  std::unordered_set<std::uint32_t> pending;
  std::unordered_set<std::uint32_t> rerun;
  std::unordered_map<std::uint32_t, std::uint32_t> finalizeDue;
  std::unordered_set<std::uint32_t> equipped;
};

SessionQueue::SessionQueue(Submit a_submit, RefreshActor a_refresh,
                           RefreshRejected a_rejected)
    : state_(std::make_shared<State>(std::move(a_submit), std::move(a_refresh),
                                     std::move(a_rejected))) {}

SessionQueue::~SessionQueue() { BeginLoad(); }

void SessionQueue::Post(Task a_task) {
  if (!a_task || !state_->submit) {
    return;
  }
  std::uint64_t generation = 0;
  {
    std::scoped_lock lock{state_->lock};
    if (state_->loading) {
      return;
    }
    generation = state_->generation;
  }
  state_->submit([weak = std::weak_ptr<State>{state_}, generation,
                  task = std::move(a_task)] {
    const std::shared_ptr<State> state = weak.lock();
    if (!state) {
      return;
    }
    {
      std::scoped_lock lock{state->lock};
      if (!state->Current(generation)) {
        return;
      }
    }
    task();
  });
}

bool SessionQueue::Refresh(std::uint32_t a_actorID) {
  std::uint64_t generation = 0;
  {
    std::scoped_lock lock{state_->lock};
    if (state_->loading) {
      return false;
    }
    generation = state_->generation;
  }
  return SubmitRefresh(state_, generation, a_actorID);
}

bool SessionQueue::SubmitRefresh(const std::shared_ptr<State> &a_state,
                                 std::uint64_t a_generation,
                                 std::uint32_t a_actorID) {
  if (a_actorID == 0 || !a_state->submit || !a_state->refresh) {
    return false;
  }
  {
    std::scoped_lock lock{a_state->lock};
    if (!a_state->Current(a_generation)) {
      return false;
    }
    if (!a_state->pending.insert(a_actorID).second) {
      a_state->rerun.insert(a_actorID);
      return true;
    }
  }
  const bool submitted = a_state->submit(
      [weak = std::weak_ptr<State>{a_state}, a_generation, a_actorID] {
        const std::shared_ptr<State> state = weak.lock();
        if (!state) {
          return;
        }
        bool rerun = false;
        {
          std::scoped_lock lock{state->lock};
          if (!state->Current(a_generation)) {
            return;
          }
          state->pending.erase(a_actorID);
          rerun = state->rerun.erase(a_actorID) > 0;
        }
        state->refresh(a_actorID);
        if (rerun) {
          SubmitRefresh(state, a_generation, a_actorID);
        }
      });
  if (!submitted) {
    std::scoped_lock lock{a_state->lock};
    if (a_state->Current(a_generation)) {
      a_state->pending.erase(a_actorID);
      a_state->rerun.erase(a_actorID);
      if (a_state->rejected) {
        a_state->rejected(a_actorID);
      }
    }
  }
  return submitted;
}

void SessionQueue::Equip(std::uint32_t a_actorID, std::uint32_t a_nowMS) {
  constexpr std::uint32_t kFinalizeDelayMS = 100;
  std::scoped_lock lock{state_->lock};
  if (a_actorID != 0 && !state_->loading) {
    state_->finalizeDue[a_actorID] = a_nowMS + kFinalizeDelayMS;
  }
}

void SessionQueue::FinalizeDue(std::uint32_t a_nowMS) {
  std::vector<std::uint32_t> due;
  std::uint64_t generation = 0;
  {
    std::scoped_lock lock{state_->lock};
    if (state_->loading) {
      return;
    }
    generation = state_->generation;
    for (auto it = state_->finalizeDue.begin();
         it != state_->finalizeDue.end();) {
      if (static_cast<std::int32_t>(a_nowMS - it->second) >= 0) {
        due.push_back(it->first);
        state_->equipped.insert(it->first);
        it = state_->finalizeDue.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (const std::uint32_t id : due) {
    SubmitRefresh(state_, generation, id);
  }
}

bool SessionQueue::TakeEquipped(std::uint32_t a_actorID) {
  std::scoped_lock lock{state_->lock};
  return !state_->loading && state_->equipped.erase(a_actorID) > 0;
}

void SessionQueue::BeginLoad() {
  std::scoped_lock lock{state_->lock};
  state_->loading = true;
  ++state_->generation;
  state_->pending.clear();
  state_->rerun.clear();
  state_->finalizeDue.clear();
  state_->equipped.clear();
}

void SessionQueue::Resume() {
  std::scoped_lock lock{state_->lock};
  state_->loading = false;
}

bool SessionQueue::Loading() const {
  std::scoped_lock lock{state_->lock};
  return state_->loading;
}
}
