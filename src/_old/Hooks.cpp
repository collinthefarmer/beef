#include "Hooks.h"

#include "Manager.h"

namespace BetterEnchantmentEffects
{
	namespace
	{
		struct PlayerUpdate
		{
			static constexpr std::size_t kIndex = 0xAD;

			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				Manager::GetSingleton()->OnFrame();
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = vtable.write_vfunc(PlayerUpdate::kIndex, PlayerUpdate::thunk);
		logger::info("hooked PlayerCharacter::Update (vfunc {:#x})", PlayerUpdate::kIndex);
	}
}
