#include "PCH.h"

#include "Manager.h"
#include "Events.h"
#include "Hooks.h"
#include "Identity.h"
#include "Menu.h"
#include "RecipeStore.h"
#include "Settings.h"

namespace BetterEnchantmentEffects
{
	std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_logRing;
}

namespace
{
	void InitLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= BetterEnchantmentEffects::Identity::LogFileName();
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		BetterEnchantmentEffects::g_logRing = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(300);
		auto log = std::make_shared<spdlog::logger>("global", spdlog::sinks_init_list{ sink, BetterEnchantmentEffects::g_logRing });
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	bool CommunityShadersLoaded()
	{
		return REX::W32::GetModuleHandleW(L"CommunityShaders.dll") != nullptr;
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		using namespace BetterEnchantmentEffects;
		auto* manager = Manager::GetSingleton();
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			logger::info("kDataLoaded");
			SetSettings(LoadSettingsFromDisk());
			LoadRecipes();
			if (!CommunityShadersLoaded()) {
				logger::error("CommunityShaders.dll is not loaded; emissive path disabled, plugin idle");
				manager->SetEmissivePathEnabled(false);
			}
			RegisterEventSinks();
			InstallHooks();
			RegisterMenu();
			manager->QueueLoadedActorRefreshes();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			logger::info("kPreLoadGame");
			manager->Clear();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			logger::info("kPostLoadGame");
			manager->QueueLoadedActorRefreshes();
			break;
		case SKSE::MessagingInterface::kNewGame:
			logger::info("kNewGame");
			manager->QueueLoadedActorRefreshes();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	InitLog();
	SKSE::Init(skse);

	const auto plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} loading on runtime {}", plugin->GetName(), plugin->GetVersion().string(), REL::Module::get().version().string());

	if (skse->IsEditor()) {
		logger::info("editor detected; doing nothing");
		return false;
	}
	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logger::error("failed to register SKSE messaging listener");
		return false;
	}
	return true;
}
