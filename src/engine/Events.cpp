// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Events.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"

#include "engine/GameObjectService.h"
#include "engine/Manager.h"

namespace BetterEnchantmentEffects {
namespace {
class EventSink : public RE::BSTEventSink<RE::TESEquipEvent>,
                  public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
                  public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>,
                  public RE::BSTEventSink<RE::TESHitEvent> {
public:
  static EventSink *GetSingleton() {
    static EventSink sink;
    return &sink;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESEquipEvent *a_event,
               RE::BSTEventSource<RE::TESEquipEvent> *) override {
    const Trace::Scope trace{Trace::Command("event.equip")};
    if (a_event && a_event->actor) {
      if (RE::Actor *actor = a_event->actor->As<RE::Actor>()) {
        Manager *manager = Manager::GetSingleton();
        manager->QueueRefresh(actor);
        if (a_event->equipped) {
          manager->QueueEquipFinalize(actor->GetFormID());
        }
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESObjectLoadedEvent *a_event,
               RE::BSTEventSource<RE::TESObjectLoadedEvent> *) override {
    if (a_event) {
      Manager *manager = Manager::GetSingleton();
      if (a_event->loaded) {
        if (RE::TESForm::LookupByID<RE::Actor>(a_event->formID)) {
          const Trace::Scope trace{Trace::Command("event.actor_loaded")};
          manager->QueueRefresh(a_event->formID);
        }
      } else {
        const Trace::Scope trace{Trace::Command("event.object_unloaded")};
        manager->QueueRetire(a_event->formID);
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESHitEvent *a_event,
               RE::BSTEventSource<RE::TESHitEvent> *) override {
    if (a_event) {
      Manager *manager = Manager::GetSingleton();
      if (a_event->target && a_event->target->As<RE::Actor>()) {
        EventRecord record;
        record.id = std::string{Studio::kHitReceivedEvent};
        record.payload.value = 1.0f;
        manager->QueueEvent(a_event->target->GetFormID(), std::move(record));
        if (a_event->cause) {
          const RE::NiPoint3 &at = a_event->cause->GetPosition();
          EventRecord carried;
          carried.id = std::string{Studio::kHitReceivedEvent} + ".position";
          carried.payload.value = Vec3{at.x, at.y, at.z};
          manager->QueueEvent(a_event->target->GetFormID(), std::move(carried));
        }
      }
      if (a_event->cause && a_event->cause->As<RE::Actor>()) {
        EventRecord record;
        record.id = std::string{Studio::kHitDealtEvent};
        record.payload.value = 1.0f;
        manager->QueueEvent(a_event->cause->GetFormID(), std::move(record));
        if (a_event->target) {
          const RE::NiPoint3 &at = a_event->target->GetPosition();
          EventRecord carried;
          carried.id = std::string{Studio::kHitDealtEvent} + ".position";
          carried.payload.value = Vec3{at.x, at.y, at.z};
          manager->QueueEvent(a_event->cause->GetFormID(), std::move(carried));
        }
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const SKSE::NiNodeUpdateEvent *a_event,
               RE::BSTEventSource<SKSE::NiNodeUpdateEvent> *) override {
    const Trace::Scope trace{Trace::Command("event.node_update")};
    if (a_event && a_event->reference) {
      if (RE::Actor *actor = a_event->reference->As<RE::Actor>()) {
        Manager::GetSingleton()->QueueRefresh(actor);
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

class AnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
  static AnimationSink *GetSingleton() {
    static AnimationSink sink;
    return &sink;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::BSAnimationGraphEvent *a_event,
               RE::BSTEventSource<RE::BSAnimationGraphEvent> *) override {
    if (a_event && a_event->holder && a_event->tag.c_str()) {
      const RE::FormID actor = a_event->holder->GetFormID();
      NoteAnimEvent(actor, a_event->tag.c_str());
      EventRecord record;
      record.id = std::string{"anim."} + a_event->tag.c_str();
      record.payload.arg =
          a_event->payload.c_str() ? a_event->payload.c_str() : "";
      Manager::GetSingleton()->QueueEvent(actor, std::move(record));
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void WatchAnimationEvents(RE::Actor *a_actor) {
  if (!a_actor) {
    return;
  }
  RE::BSAnimationGraphManagerPtr manager;
  if (!a_actor->GetAnimationGraphManager(manager) || !manager) {
    return;
  }
  for (const auto &graph : manager->graphs) {
    if (graph) {
      graph->GetEventSource<RE::BSAnimationGraphEvent>()->AddEventSink(
          AnimationSink::GetSingleton());
      Metrics::CountSinkAdd();
      return;
    }
  }
}

void UnwatchAnimationEvents(RE::Actor *a_actor) {
  if (!a_actor) {
    return;
  }
  RE::BSAnimationGraphManagerPtr manager;
  if (!a_actor->GetAnimationGraphManager(manager) || !manager) {
    return;
  }
  for (const auto &graph : manager->graphs) {
    if (graph) {
      graph->GetEventSource<RE::BSAnimationGraphEvent>()->RemoveEventSink(
          AnimationSink::GetSingleton());
      Metrics::CountSinkRemove();
    }
  }
}

void RegisterEventSinks() {
  EventSink *sink = EventSink::GetSingleton();
  if (RE::ScriptEventSourceHolder *holder =
          RE::ScriptEventSourceHolder::GetSingleton()) {
    holder->AddEventSink<RE::TESEquipEvent>(sink);
    holder->AddEventSink<RE::TESObjectLoadedEvent>(sink);
    holder->AddEventSink<RE::TESHitEvent>(sink);
  } else {
    logger::error(
        "ScriptEventSourceHolder unavailable; equip and load events will not "
        "be seen");
  }
  if (RE::BSTEventSource<SKSE::NiNodeUpdateEvent> *source =
          SKSE::GetNiNodeUpdateEventSource()) {
    source->AddEventSink(sink);
  } else {
    logger::error("SKSE NiNodeUpdateEvent source unavailable");
  }
  logger::info("event sinks registered");
}
}
