#include "Hooks.h"

#include "Manager.h"

namespace WornEnchantmentPBR
{
	namespace
	{
		struct PlayerUpdate
		{
			// Actor::Update is vfunc 0xAD in RE/A/Actor.h (SE/AE; VR differs and
			// is not built).
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
