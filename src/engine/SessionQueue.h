// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace BetterEnchantmentEffects {
class SessionQueue {
public:
  using Task = std::function<void()>;
  using Submit = std::function<bool(Task)>;
  using RefreshActor = std::function<void(std::uint32_t)>;
  using RefreshRejected = std::function<void(std::uint32_t)>;

  SessionQueue(Submit a_submit, RefreshActor a_refresh,
               RefreshRejected a_rejected = {});
  ~SessionQueue();
  SessionQueue(const SessionQueue &) = delete;
  SessionQueue &operator=(const SessionQueue &) = delete;
  SessionQueue(SessionQueue &&) = delete;
  SessionQueue &operator=(SessionQueue &&) = delete;

  void Post(Task a_task);
  bool Refresh(std::uint32_t a_actorID);
  void Equip(std::uint32_t a_actorID, std::uint32_t a_nowMS);
  void FinalizeDue(std::uint32_t a_nowMS);
  [[nodiscard]] bool TakeEquipped(std::uint32_t a_actorID);

  void BeginLoad();
  void Resume();
  [[nodiscard]] bool Loading() const;

private:
  struct State;
  static bool SubmitRefresh(const std::shared_ptr<State> &a_state,
                            std::uint64_t a_generation,
                            std::uint32_t a_actorID);
  std::shared_ptr<State> state_;
};
}
