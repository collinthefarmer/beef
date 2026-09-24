#include "engine/Hooks.h"

#include "engine/Manager.h"

#include <atomic>
#include <mutex>
#include <string_view>

namespace BetterEnchantmentEffects {
namespace {
std::atomic_bool g_hookFailed{false};
constexpr std::string_view kHookProblem =
    "Player update hook could not resolve its vtable; effects are disabled. "
    "Check that Skyrim, SKSE, Address Library, and the plugin target match, "
    "then restart Skyrim. See the plugin and SKSE loader logs.";

struct PlayerUpdate {
  static constexpr std::size_t kIndex = 0xAD;

  static void thunk(RE::PlayerCharacter *a_this, float a_delta) {
    func(a_this, a_delta);
    Manager::GetSingleton()->OnFrame();
  }

  static inline REL::Relocation<decltype(thunk)> func;
};
}

bool InstallHooks() {
  static bool ready = false;
  static std::once_flag installed;
  std::call_once(installed, [] {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_PlayerCharacter[0]};
    if (!vtable.address()) {
      g_hookFailed.store(true);
      logger::error("{}", kHookProblem);
      return;
    }
    PlayerUpdate::func =
        vtable.write_vfunc(PlayerUpdate::kIndex, PlayerUpdate::thunk);
    ready = true;
    logger::info("hooked PlayerCharacter::Update (vfunc {:#x})",
                 PlayerUpdate::kIndex);
  });
  return ready;
}

std::string_view HookProblem() {
  return g_hookFailed.load() ? kHookProblem : std::string_view{};
}
}
