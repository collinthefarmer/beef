// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace RE {
using FormID = std::uint32_t;
template <class T> using BSTSmartPointer = std::shared_ptr<T>;
enum class BSEventNotifyControl { kContinue };
template <class T> struct BSTEventSource;
template <class T> struct BSTEventSink {
  virtual ~BSTEventSink() = default;
  virtual BSEventNotifyControl ProcessEvent(const T *, BSTEventSource<T> *) = 0;
};
template <class T> struct BSTEventSource {
  std::recursive_mutex lock;
  std::vector<BSTEventSink<T> *> sinks;
  std::function<void(BSTEventSink<T> *)> adding;
  std::function<void(BSTEventSink<T> *)> removing;
  int adds = 0;
  int removes = 0;

  void AddEventSink(BSTEventSink<T> *a_sink) {
    const std::scoped_lock guard{lock};
    ++adds;
    if (adding)
      adding(a_sink);
    if (std::ranges::find(sinks, a_sink) == sinks.end())
      sinks.push_back(a_sink);
  }
  void RemoveEventSink(BSTEventSink<T> *a_sink) {
    const std::scoped_lock guard{lock};
    ++removes;
    if (removing)
      removing(a_sink);
    std::erase(sinks, a_sink);
  }
  void Send(const T &a_event) {
    const std::scoped_lock guard{lock};
    for (auto *sink : sinks)
      sink->ProcessEvent(&a_event, this);
  }
};
struct Actor;
struct FixedString {
  const char *value = nullptr;
  const char *c_str() const { return value; }
};
struct BSAnimationGraphEvent {
  FixedString tag;
  FixedString payload;
  Actor *holder = nullptr;
};
struct BShkbAnimationGraph : BSTEventSource<BSAnimationGraphEvent> {
  template <class T> BSTEventSource<T> *GetEventSource() { return this; }
};
struct BSAnimationGraphManager {
  std::vector<BSTSmartPointer<BShkbAnimationGraph>> graphs;
};
using BSAnimationGraphManagerPtr = std::shared_ptr<BSAnimationGraphManager>;
struct ActorHandle {
  std::weak_ptr<Actor> actor;
  std::shared_ptr<Actor> get() const { return actor.lock(); }
};
struct Actor : std::enable_shared_from_this<Actor> {
  FormID id = 1;
  bool deleted = false;
  bool loaded = true;
  BSAnimationGraphManagerPtr manager =
      std::make_shared<BSAnimationGraphManager>();
  FormID GetFormID() const { return id; }
  bool IsDeleted() const { return deleted; }
  bool Is3DLoaded() const { return loaded; }
  ActorHandle GetHandle() { return {weak_from_this()}; }
  bool GetAnimationGraphManager(BSAnimationGraphManagerPtr &a_manager) {
    a_manager = manager;
    return bool(manager);
  }
};
}
