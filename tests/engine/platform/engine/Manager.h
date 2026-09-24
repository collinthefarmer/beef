#pragma once

#include "PCH.h"
#include "engine/SessionQueue.h"

#include <deque>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects {
class Manager {
public:
  std::deque<SessionQueue::Task> tasks;
  SessionQueue queue{[this](SessionQueue::Task task) {
                       tasks.push_back(std::move(task));
                       return true;
                     },
                     {}};

  Manager() { queue.Resume(); }
  void PostTask(std::function<void()> task) { queue.Post(std::move(task)); }
  void ChangeAndRebuildActors(std::string, std::function<void()> change) {
    change();
  }
  void Drain() {
    while (!tasks.empty()) {
      auto task = std::move(tasks.front());
      tasks.pop_front();
      task();
    }
  }
};
}
