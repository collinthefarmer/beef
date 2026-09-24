// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Hooks.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void Original(RE::PlayerCharacter *, float) { HookFixture::calls.push_back(1); }
}

int main(int a_argc, char **) {
  Check(HookProblem().empty(),
        "no hook failure is reported before installation");
  if (a_argc > 1) {
    RE::VTABLE_PlayerCharacter[0] = 0x1000;
    HookFixture::original = reinterpret_cast<std::uintptr_t>(&Original);
    Check(InstallHooks(), "a resolved vtable installs the hook");
    Check(InstallHooks() && HookFixture::patches == 1,
          "successful repeated installation does not patch twice");
    Check(HookFixture::slot == 0xAD && HookFixture::replacement != 0,
          "installation replaces the reviewed player update slot");
    Check(HookProblem().empty() && HookFixture::errors.empty(),
          "successful installation leaves no failure message");
    if (HookFixture::replacement) {
      reinterpret_cast<void (*)(RE::PlayerCharacter *, float)>(
          HookFixture::replacement)(nullptr, 0.016f);
      Check(HookFixture::calls == std::vector<int>{1, 2},
            "the hook calls the original update before the manager frame");
    }
  } else {
    Check(!InstallHooks() && HookFixture::patches == 0,
          "a zero vtable refuses installation before any slot access");
    Check(HookProblem().find("effects are disabled") !=
                  std::string_view::npos &&
              HookProblem().find("Address Library") != std::string_view::npos &&
              HookProblem().find("restart") != std::string_view::npos,
          "failed installation exposes an actionable startup error");
    RE::VTABLE_PlayerCharacter[0] = 0x1000;
    Check(!InstallHooks() && HookFixture::patches == 0 &&
              HookFixture::errors.size() == 1,
          "failed installation stays failed until restart without retry or log "
          "spam");
  }
  return test::Finish("engine_hooks");
}
