#include "diagnostics/Trace.h"
#include "engine/SessionQueue.h"
#include "test_support.h"

#include <deque>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

static_assert(!std::is_copy_constructible_v<SessionQueue>);
static_assert(!std::is_move_constructible_v<SessionQueue>);

namespace {
struct Scheduler {
  bool Submit(SessionQueue::Task a_task) {
    if (beforeSubmit) {
      SessionQueue::Task hook = std::exchange(beforeSubmit, {});
      hook();
    }
    if (!accept) {
      return false;
    }
    tasks.push_back(std::move(a_task));
    return true;
  }

  void Run() {
    if (tasks.empty()) {
      return;
    }
    SessionQueue::Task task = std::move(tasks.front());
    tasks.pop_front();
    task();
  }

  bool accept = true;
  SessionQueue::Task beforeSubmit;
  std::deque<SessionQueue::Task> tasks;
};

void LoadAndCoalesce() {
  Scheduler scheduler;
  std::vector<std::uint32_t> refreshed;
  int posted = 0;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       return scheduler.Submit(std::move(a_task));
                     },
                     [&](std::uint32_t a_id) { refreshed.push_back(a_id); }};
  Check(queue.Loading(), "the queue starts paused");
  queue.Post([&] { ++posted; });
  queue.Refresh(42);
  queue.Equip(42, 0);
  queue.Resume();
  queue.FinalizeDue(100);
  Check(scheduler.tasks.empty(), "work submitted while loading is dropped");
  queue.Refresh(0);
  Check(scheduler.tasks.empty(), "zero actor IDs are rejected");
  queue.Refresh(42);
  queue.Refresh(42);
  queue.Refresh(42);
  Check(scheduler.tasks.size() == 1, "duplicate refreshes share one task");
  scheduler.Run();
  Check(refreshed == std::vector<std::uint32_t>{42} &&
            scheduler.tasks.size() == 1,
        "duplicates schedule one follow-up refresh");
  scheduler.Run();
  Check(refreshed == std::vector<std::uint32_t>{42, 42} &&
            scheduler.tasks.empty(),
        "the follow-up does not repeat indefinitely");
  queue.Post([&] { ++posted; });
  queue.Refresh(42);
  queue.BeginLoad();
  queue.Resume();
  queue.Refresh(42);
  scheduler.Run();
  scheduler.Run();
  Check(posted == 0 && refreshed.size() == 2,
        "old tasks remain stale after the next session resumes");
  queue.Refresh(42);
  Check(scheduler.tasks.size() == 1,
        "a stale task cannot erase the new session's pending marker");
  scheduler.Run();
  scheduler.Run();
  Check(refreshed.size() == 4, "the new session and its follow-up run");
}

void SubmissionFailure() {
  Scheduler scheduler;
  int refreshed = 0;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       return scheduler.Submit(std::move(a_task));
                     },
                     [&](std::uint32_t) { ++refreshed; }};
  queue.Resume();
  scheduler.accept = false;
  queue.Refresh(42);
  scheduler.accept = true;
  queue.Refresh(42);
  Check(scheduler.tasks.size() == 1,
        "failed submission releases pending state for a retry");
  scheduler.Run();
  Check(refreshed == 1 && scheduler.tasks.empty(),
        "a retry does not inherit a phantom duplicate");
}

void LoadDuringSubmission() {
  Scheduler scheduler;
  int refreshed = 0;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       return scheduler.Submit(std::move(a_task));
                     },
                     [&](std::uint32_t) { ++refreshed; }};
  queue.Resume();
  scheduler.beforeSubmit = [&] {
    queue.BeginLoad();
    queue.Resume();
    queue.Refresh(42);
  };
  queue.Refresh(42);
  Check(scheduler.tasks.size() == 2,
        "load can intervene between reservation and submission");
  scheduler.Run();
  scheduler.Run();
  Check(refreshed == 1 && scheduler.tasks.empty(),
        "work reserved before loading cannot enter the new session");
}

void LoadDuringRefresh() {
  Scheduler scheduler;
  int refreshed = 0;
  SessionQueue *current = nullptr;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       return scheduler.Submit(std::move(a_task));
                     },
                     [&](std::uint32_t) {
                       ++refreshed;
                       current->BeginLoad();
                       current->Resume();
                     }};
  current = &queue;
  queue.Resume();
  queue.Refresh(42);
  queue.Refresh(42);
  scheduler.Run();
  Check(refreshed == 1 && scheduler.tasks.empty(),
        "a follow-up cannot cross a load triggered during refresh");
}

void EquipmentDelay() {
  Scheduler scheduler;
  int refreshed = 0;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       return scheduler.Submit(std::move(a_task));
                     },
                     [&](std::uint32_t) { ++refreshed; }};
  queue.Resume();
  queue.Equip(42, 1000);
  queue.Equip(42, 1050);
  queue.FinalizeDue(1100);
  Check(scheduler.tasks.empty() && !queue.TakeEquipped(42),
        "a later equip restarts the finalize delay");
  queue.FinalizeDue(1150);
  Check(queue.TakeEquipped(42) && !queue.TakeEquipped(42),
        "equipment notification is consumed once");
  scheduler.Run();
  Check(refreshed == 1, "finalizing queues the actor refresh");
  queue.Equip(42, std::numeric_limits<std::uint32_t>::max() - 49);
  queue.FinalizeDue(49);
  Check(scheduler.tasks.empty(), "the delay survives timer wraparound");
  queue.FinalizeDue(50);
  Check(scheduler.tasks.size() == 1 && queue.TakeEquipped(42),
        "wrapped deadline becomes due at the right time");
  queue.Equip(99, 100);
  queue.BeginLoad();
  queue.Resume();
  queue.FinalizeDue(200);
  scheduler.Run();
  Check(refreshed == 1 && scheduler.tasks.empty() && !queue.TakeEquipped(99),
        "loading clears timers, notifications, and queued finalizes");
}

void QueueLifetime() {
  Scheduler scheduler;
  int calls = 0;
  {
    SessionQueue queue{[&](SessionQueue::Task a_task) {
                         return scheduler.Submit(std::move(a_task));
                       },
                       [&](std::uint32_t) { ++calls; }};
    queue.Resume();
    queue.Post([&] { ++calls; });
    queue.Refresh(42);
  }
  scheduler.Run();
  scheduler.Run();
  Check(calls == 0, "queued callbacks do not outlive their queue's owner");
}
}

int main() {
  {
    Scheduler scheduler;
    std::uint64_t seen = 0;
    SessionQueue queue{[&](SessionQueue::Task task) {
                         return scheduler.Submit(std::move(task));
                       },
                       [&](std::uint32_t) { seen = Trace::Current().command; }};
    queue.Resume();
    const auto cause = Trace::Command("test.refresh");
    {
      const Trace::Scope scope{cause};
      queue.Refresh(42);
    }
    scheduler.Run();
    Check(seen == cause.command,
          "queued refresh retains its originating command");
    Check(Trace::Current().command == 0,
          "refresh context is cleared after execution");
    {
      const Trace::Scope scope{cause};
      queue.Post([&] { seen = Trace::Current().command; });
    }
    seen = 0;
    scheduler.Run();
    Check(seen == cause.command, "posted task retains its originating command");
  }
  LoadAndCoalesce();
  SubmissionFailure();
  LoadDuringSubmission();
  LoadDuringRefresh();
  EquipmentDelay();
  QueueLifetime();
  return test::Finish("session queue");
}
