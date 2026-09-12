#pragma once

#include "PCH.h"
#include "recipe/Efsh.h"
#include "recipe/Signals.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace BetterEnchantmentEffects {
class ActorEnvironment final : public SignalEnvironment {
public:
  ActorEnvironment(RE::Actor *a_actor, RE::MagicItem *a_enchantment);

  [[nodiscard]] float ActorValue(std::string_view a_name,
                                 Measure a_measure) const override;
  [[nodiscard]] float ActorState(ActorStateKind a_kind) const override;
  [[nodiscard]] float Enchantment(EnchantmentField a_field) const override;
  [[nodiscard]] std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &a_record) const override;

private:
  [[nodiscard]] RE::NiPointer<RE::Actor> Actor() const noexcept;

  RE::ActorHandle actor_;
  RE::FormID enchantment_ = 0;
  mutable std::unordered_map<std::string, RE::ActorValue> actorValues_;
  mutable std::unordered_map<std::string, std::optional<Efsh::EffectParams>>
      shaders_;
};
}
