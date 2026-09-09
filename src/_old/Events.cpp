#include "Events.h"

#include "Manager.h"

namespace BetterEnchantmentEffects
{
	namespace
	{
		class EventSink :
			public RE::BSTEventSink<RE::TESEquipEvent>,
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
			public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>,
			public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			static EventSink* GetSingleton()
			{
				static EventSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
			{
				if (a_event && a_event->actor) {
					if (auto* actor = a_event->actor->As<RE::Actor>()) {
						auto* manager = Manager::GetSingleton();
						manager->QueueRefresh(actor);
						if (a_event->equipped) {
							manager->QueueEquipFinalize(actor->GetFormID());
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (a_event) {
					auto* manager = Manager::GetSingleton();
					if (a_event->loaded) {
						if (RE::TESForm::LookupByID<RE::Actor>(a_event->formID)) {
							manager->QueueRefresh(a_event->formID);
						}
					} else {
						manager->QueueRetire(a_event->formID);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (a_event) {
					auto* manager = Manager::GetSingleton();
					if (a_event->target && a_event->target->As<RE::Actor>()) {
						EventRecord record;
						record.id = "hit.received";
						record.payload.value = 1.0f;
						manager->QueueEvent(a_event->target->GetFormID(), std::move(record));
					}
					if (a_event->cause && a_event->cause->As<RE::Actor>()) {
						EventRecord record;
						record.id = "hit.dealt";
						record.payload.value = 1.0f;
						manager->QueueEvent(a_event->cause->GetFormID(), std::move(record));
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event, RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) override
			{
				if (a_event && a_event->reference) {
					if (auto* actor = a_event->reference->As<RE::Actor>()) {
						Manager::GetSingleton()->QueueRefresh(actor);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	namespace
	{
		class AnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			static AnimationSink* GetSingleton()
			{
				static AnimationSink sink;
				return &sink;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (a_event && a_event->holder && a_event->tag.c_str()) {
					EventRecord record;
					record.id = std::string{ "anim." } + a_event->tag.c_str();
					record.payload.arg = a_event->payload.c_str() ? a_event->payload.c_str() : "";
					Manager::GetSingleton()->QueueEvent(a_event->holder->GetFormID(), std::move(record));
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void WatchAnimationEvents(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}
		auto* sink = AnimationSink::GetSingleton();
		a_actor->RemoveAnimationGraphEventSink(sink);
		a_actor->AddAnimationGraphEventSink(sink);
	}

	void UnwatchAnimationEvents(RE::Actor* a_actor)
	{
		if (a_actor) {
			a_actor->RemoveAnimationGraphEventSink(AnimationSink::GetSingleton());
		}
	}

	void RegisterEventSinks()
	{
		auto* sink = EventSink::GetSingleton();
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESEquipEvent>(sink);
			holder->AddEventSink<RE::TESObjectLoadedEvent>(sink);
			holder->AddEventSink<RE::TESHitEvent>(sink);
		} else {
			logger::error("ScriptEventSourceHolder unavailable; equip and load events will not be seen");
		}
		if (auto* source = SKSE::GetNiNodeUpdateEventSource()) {
			source->AddEventSink(sink);
		} else {
			logger::error("SKSE NiNodeUpdateEvent source unavailable");
		}
		logger::info("event sinks registered");
	}
}
