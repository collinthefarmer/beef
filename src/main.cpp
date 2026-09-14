#include "PCH.h"

#include "BuildIdentity.h"
#include "Identity.h"
#include "SettingsFile.h"
#include "diagnostics/Trace.h"
#include "engine/Events.h"
#include "engine/Hooks.h"
#include "engine/Manager.h"
#include "engine/RecipeStore.h"
#include "menu/Menu.h"
#include <chrono>

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
    logger::info("diagnostic trace: {} ({} MiB segments, the last {} kept)",
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
    manager->SetEmissivePathEnabled(available);
    if (!available) {
      logger::error("CommunityShaders.dll is not loaded; emissive path "
                    "disabled, plugin idle");
      break;
    }
    RegisterEventSinks();
    InstallHooks();
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
