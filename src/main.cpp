// GPL-3.0-only with the additional permission in COPYING.md.
#include "PCH.h"

#include "BuildCompatibility.h"
#include "BuildIdentity.h"
#include "Identity.h"
#include "SettingsFile.h"
#include "diagnostics/Trace.h"
#include "engine/Events.h"
#include "engine/Hooks.h"
#include "engine/Manager.h"
#include "engine/PluginEvents.h"
#include "engine/RecipeStore.h"
#include "menu/Menu.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace BetterEnchantmentEffects {
std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_logRing;
}

namespace {
void InitLog() {
  auto path = logger::log_directory();
  if (!path) {
    return;
  }
  const auto run =
      std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count());
  const auto tracePath =
      *path / BetterEnchantmentEffects::Identity::TraceFileName(run);
  const bool traceOpened =
      BetterEnchantmentEffects::Trace::Get().Open(tracePath, run);
  *path /= BetterEnchantmentEffects::Identity::LogFileName();
  auto sink =
      std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
  BetterEnchantmentEffects::g_logRing =
      std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(300);
  auto log = std::make_shared<spdlog::logger>(
      "global",
      spdlog::sinks_init_list{sink, BetterEnchantmentEffects::g_logRing});
  log->set_level(spdlog::level::info);
  log->flush_on(spdlog::level::info);
  spdlog::set_default_logger(std::move(log));
  spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
  logger::info("build: {} source SHA256 {}",
               BetterEnchantmentEffects::BuildIdentity::build,
               BetterEnchantmentEffects::BuildIdentity::source_sha256);
  if (traceOpened) {
    logger::info(
        "diagnostic trace: {} ({} MiB segments, the first and the last {} "
        "kept)",
        tracePath.string(),
        BetterEnchantmentEffects::Trace::kTraceSegmentBytes /
            (std::uint64_t{1024} * 1024),
        BetterEnchantmentEffects::Trace::kTraceSegmentsKept);
  } else {
    logger::warn("diagnostic trace could not be opened: {}",
                 tracePath.string());
  }
}

bool CommunityShadersLoaded() {
  return REX::W32::GetModuleHandleW(L"CommunityShaders.dll") != nullptr;
}

void OnPluginEvent(SKSE::MessagingInterface::Message *a_msg) {
  using namespace BetterEnchantmentEffects;
  if (!a_msg || a_msg->type != kPluginEventMessage) {
    return;
  }
  auto event = ParsePluginEvent(a_msg->data, a_msg->dataLen);
  if (!event) {
    logger::warn("plugin event from '{}': not a valid PluginEventMessage",
                 a_msg->sender ? a_msg->sender : "?");
    return;
  }
  auto *manager = Manager::GetSingleton();
  if (event->actor == 0) {
    manager->QueueBroadcast(std::move(event->record));
  } else {
    manager->QueueEvent(event->actor, std::move(event->record));
  }
}

void OnMessage(SKSE::MessagingInterface::Message *a_msg) {
  using namespace BetterEnchantmentEffects;
  if (!a_msg) {
    return;
  }
  auto *manager = Manager::GetSingleton();
  switch (a_msg->type) {
  case SKSE::MessagingInterface::kDataLoaded: {
    static bool initialized = false;
    if (initialized) {
      return;
    }
    initialized = true;
    logger::info("kDataLoaded");
    SetSettings(LoadSettingsFromDisk());
    LoadRecipes();
    for (const Recipe &recipe : LoadedRecipes()) {
      Trace::Emit(Trace::Event::kRecipe,
                  {{"id", recipe.id},
                   {"fingerprint_fnv1a64",
                    Trace::Fingerprint(SerializeRecipe(recipe))}});
    }
    Menu::RegisterMenu();
    const bool available = CommunityShadersLoaded();
    manager->SetEmissivePathEnabled(false);
    if (!available) {
      logger::error("CommunityShaders.dll is not loaded; emissive path "
                    "disabled, plugin idle");
      logger::error("Check the Community Shaders installation and SKSE loader "
                    "log, then restart Skyrim; all effects require it.");
      break;
    }
    if (!InstallHooks()) {
      break;
    }
    manager->SetEmissivePathEnabled(true);
    RegisterEventSinks();
    break;
  }
  case SKSE::MessagingInterface::kPreLoadGame:
    logger::info("kPreLoadGame");
    manager->BeginLoad();
    break;
  case SKSE::MessagingInterface::kPostLoadGame:
    logger::info("kPostLoadGame");
    if (a_msg->data) {
      manager->FinishLoad();
    } else {
      logger::warn("save load failed; engine effects remain paused");
    }
    break;
  case SKSE::MessagingInterface::kNewGame:
    logger::info("kNewGame");
    manager->BeginLoad();
    manager->FinishLoad();
    break;
  default:
    break;
  }
}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
  InitLog();
  if (!skse || skse->IsEditor()) {
    return false;
  }
  if (!BetterEnchantmentEffects::BuildCompatibility::SupportsRuntime(
          skse->RuntimeVersion().pack()) ||
      skse->SKSEVersion() <
          BetterEnchantmentEffects::BuildCompatibility::minimumSKSE) {
    logger::error("compatibility profile {} refuses runtime {} / SKSE {}; "
                  "install the matching targeted build",
                  BetterEnchantmentEffects::BuildCompatibility::profile,
                  skse->RuntimeVersion().string(), skse->SKSEVersion());
    return false;
  }
  logger::info("compatibility profile {} (runtime acceptance pending)",
               BetterEnchantmentEffects::BuildCompatibility::profile);
  SKSE::Init(skse);

  const auto plugin = SKSE::PluginDeclaration::GetSingleton();
  logger::info("{} {} loading on runtime {}", plugin->GetName(),
               plugin->GetVersion().string(),
               REL::Module::get().version().string());

  BetterEnchantmentEffects::Trace::Emit(
      BetterEnchantmentEffects::Trace::Event::kStartup,
      {{"build", BetterEnchantmentEffects::BuildIdentity::build},
       {"source_sha256",
        BetterEnchantmentEffects::BuildIdentity::source_sha256},
       {"runtime", REL::Module::get().version().string()},
       {"skse_packed", std::to_string(skse->SKSEVersion())}});

  if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
    logger::error("failed to register SKSE messaging listener");
    return false;
  }
  if (!SKSE::GetMessagingInterface()->RegisterListener(nullptr,
                                                       OnPluginEvent)) {
    logger::error("failed to register the plugin event listener; trigger "
                  "signals with a plugin origin will not fire");
  }
  return true;
}
