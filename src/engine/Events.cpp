// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Events.h"
#include "diagnostics/Trace.h"
#include "recipe/BusEvents.h"

#include "engine/Manager.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
constexpr RE::BSEventNotifyControl kContinue =
    RE::BSEventNotifyControl::kContinue;

void QueueHit(RE::TESObjectREFR *a_actor, RE::TESObjectREFR *a_other,
              std::string_view a_event) {
  if (!a_actor || !a_actor->As<RE::Actor>()) {
    return;
  }
  Manager *manager = Manager::GetSingleton();
  EventRecord record;
  record.id = std::string{a_event};
  record.payload.value = 1.0f;
  manager->QueueEvent(a_actor->GetFormID(), std::move(record));
  if (!a_other) {
    return;
  }
  const RE::NiPoint3 &at = a_other->GetPosition();
  EventRecord carried;
  carried.id = PositionEventId(a_event);
  carried.payload.value = Vec3{at.x, at.y, at.z};
  manager->QueueEvent(a_actor->GetFormID(), std::move(carried));
}

struct SchoolSkill {
  RE::ActorValue skill;
  SpellSchool school;
};

constexpr SchoolSkill kSchoolSkills[]{
    {RE::ActorValue::kAlteration, SpellSchool::kAlteration},
    {RE::ActorValue::kConjuration, SpellSchool::kConjuration},
    {RE::ActorValue::kDestruction, SpellSchool::kDestruction},
    {RE::ActorValue::kIllusion, SpellSchool::kIllusion},
    {RE::ActorValue::kRestoration, SpellSchool::kRestoration}};

std::optional<SpellSchool> SchoolOf(const RE::MagicItem &a_spell) {
  const RE::Effect *effect = a_spell.GetCostliestEffectItem();
  const RE::EffectSetting *base = effect ? effect->baseEffect : nullptr;
  if (!base) {
    return std::nullopt;
  }
  const auto *found = std::ranges::find(kSchoolSkills, base->GetMagickSkill(),
                                        &SchoolSkill::skill);
  return found != std::ranges::end(kSchoolSkills) ? std::optional{found->school}
                                                  : std::nullopt;
}

void QueueSpellCast(RE::Actor &a_caster, RE::MagicItem &a_spell) {
  const std::optional<SpellSchool> school = SchoolOf(a_spell);
  if (!school) {
    return;
  }
  const float cost = a_spell.CalculateMagickaCost(&a_caster);
  EventRecord record;
  record.id = std::string{SpellCastEventId(*school)};
  record.payload.value = std::isfinite(cost) ? cost : 0.0f;
  Manager::GetSingleton()->QueueEvent(a_caster.GetFormID(), std::move(record));
}

class EventSink : public RE::BSTEventSink<RE::TESEquipEvent>,
                  public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
                  public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>,
                  public RE::BSTEventSink<RE::TESHitEvent>,
                  public RE::BSTEventSink<RE::TESSpellCastEvent> {
public:
  static EventSink *GetSingleton() {
    static EventSink sink;
    return &sink;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESEquipEvent *a_event,
               RE::BSTEventSource<RE::TESEquipEvent> *) override {
    const Trace::Scope trace{Trace::Operation("event.equip")};
    if (!a_event || !a_event->actor) {
      return kContinue;
    }
    RE::Actor *actor = a_event->actor->As<RE::Actor>();
    if (!actor) {
      return kContinue;
    }

    Manager *manager = Manager::GetSingleton();
    manager->MarkStale(actor);
    if (a_event->equipped) {
      manager->QueueEquipFinalize(actor->GetFormID());
    }
    return kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESObjectLoadedEvent *a_event,
               RE::BSTEventSource<RE::TESObjectLoadedEvent> *) override {
    if (!a_event) {
      return kContinue;
    }
    Manager *manager = Manager::GetSingleton();
    if (!a_event->loaded) {
      const Trace::Scope trace{Trace::Operation("event.object_unloaded")};
      manager->QueueForget(a_event->formID);
      return kContinue;
    }
    if (!RE::TESForm::LookupByID<RE::Actor>(a_event->formID)) {
      return kContinue;
    }
    const Trace::Scope trace{Trace::Operation("event.actor_loaded")};
    manager->MarkStale(a_event->formID);
    return kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESHitEvent *a_event,
               RE::BSTEventSource<RE::TESHitEvent> *) override {
    if (!a_event) {
      return kContinue;
    }
    QueueHit(a_event->target.get(), a_event->cause.get(), kHitReceivedEvent);
    QueueHit(a_event->cause.get(), a_event->target.get(), kHitDealtEvent);
    return kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const RE::TESSpellCastEvent *a_event,
               RE::BSTEventSource<RE::TESSpellCastEvent> *) override {
    RE::Actor *caster =
        a_event && a_event->object ? a_event->object->As<RE::Actor>() : nullptr;
    RE::TESForm *form =
        caster ? RE::TESForm::LookupByID(a_event->spell) : nullptr;
    RE::MagicItem *spell = form ? form->As<RE::MagicItem>() : nullptr;
    if (spell) {
      QueueSpellCast(*caster, *spell);
    }
    return kContinue;
  }

  RE::BSEventNotifyControl
  ProcessEvent(const SKSE::NiNodeUpdateEvent *a_event,
               RE::BSTEventSource<SKSE::NiNodeUpdateEvent> *) override {
    const Trace::Scope trace{Trace::Operation("event.node_update")};
    if (!a_event || !a_event->reference) {
      return kContinue;
    }
    RE::Actor *actor = a_event->reference->As<RE::Actor>();
    if (!actor) {
      return kContinue;
    }
    Manager::GetSingleton()->MarkStale(actor);
    return kContinue;
  }
};

void RegisterScriptEvents(EventSink *a_sink) {
  RE::ScriptEventSourceHolder *holder =
      RE::ScriptEventSourceHolder::GetSingleton();
  if (!holder) {
    logger::error(
        "ScriptEventSourceHolder unavailable; equip, load, hit and spell-cast "
        "events will not be seen");
    return;
  }
  holder->AddEventSink<RE::TESEquipEvent>(a_sink);
  holder->AddEventSink<RE::TESObjectLoadedEvent>(a_sink);
  holder->AddEventSink<RE::TESHitEvent>(a_sink);
  holder->AddEventSink<RE::TESSpellCastEvent>(a_sink);
}

void RegisterNodeUpdates(EventSink *a_sink) {
  RE::BSTEventSource<SKSE::NiNodeUpdateEvent> *source =
      SKSE::GetNiNodeUpdateEventSource();
  if (!source) {
    logger::error("SKSE NiNodeUpdateEvent source unavailable");
    return;
  }
  source->AddEventSink(a_sink);
}
}

void RegisterEventSinks() {
  EventSink *sink = EventSink::GetSingleton();
  RegisterScriptEvents(sink);
  RegisterNodeUpdates(sink);
  logger::info("event sinks registered");
}
}
