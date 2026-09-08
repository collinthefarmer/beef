#pragma once

#include "PCH.h"
#include "Signals.h"

#include <string>
#include <unordered_map>

namespace WornEnchantmentPBR
{
	class ActorEnvironment final : public SignalEnvironment
	{
	public:
		ActorEnvironment(RE::Actor* a_actor, RE::MagicItem* a_enchantment);

		[[nodiscard]] float                               ActorValue(std::string_view a_name, Measure a_measure) const override;
		[[nodiscard]] float                               ActorState(ActorStateKind a_kind) const override;
		[[nodiscard]] float                               Enchantment(EnchantmentField a_field) const override;
		[[nodiscard]] std::optional<Timing::EffectParams> EffectShader(const FormRef& a_record) const override;

	private:
		[[nodiscard]] RE::Actor* Actor() const noexcept;

		RE::ActorHandle actor_;
		RE::FormID      enchantment_ = 0;
		mutable std::unordered_map<std::string, RE::ActorValue>            actorValues_;
		mutable std::unordered_map<std::string, std::optional<Timing::EffectParams>> shaders_;
	};
}
