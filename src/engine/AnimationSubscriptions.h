// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects {
struct AnimationRegistration {
  RE::FormID actor = 0;
  std::atomic_bool active{true};
};

struct AnimationEvent {
  std::shared_ptr<const AnimationRegistration> registration;
  std::string tag;
  std::string payload;

  [[nodiscard]] bool Current() const noexcept;
};

class AnimationSubscriptions final
    : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
  using Submit = std::function<void(AnimationEvent)>;

  explicit AnimationSubscriptions(Submit a_submit);
  ~AnimationSubscriptions() override;
  AnimationSubscriptions(const AnimationSubscriptions &) = delete;
  AnimationSubscriptions &operator=(const AnimationSubscriptions &) = delete;

  void Reconcile(RE::Actor &a_actor);
  void Stop(RE::FormID a_actor);
  void Clear();
  [[nodiscard]] std::vector<RE::FormID> Actors() const;
  [[nodiscard]] bool Accept(const AnimationEvent &a_event, RE::Actor &a_actor);

  RE::BSEventNotifyControl ProcessEvent(
      const RE::BSAnimationGraphEvent *a_event,
      RE::BSTEventSource<RE::BSAnimationGraphEvent> *a_source) override;

private:
  using Source = RE::BSTEventSource<RE::BSAnimationGraphEvent>;
  struct Subscription {
    RE::ActorHandle actor;
    RE::BSTSmartPointer<RE::BShkbAnimationGraph> graph;
    std::shared_ptr<AnimationRegistration> registration;
  };

  Submit submit_;
  std::unordered_map<RE::FormID, Subscription> subscriptions_;
  std::mutex sourceLock_;
  std::unordered_map<Source *, std::shared_ptr<AnimationRegistration>> sources_;
};
}
