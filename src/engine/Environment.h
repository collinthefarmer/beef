// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "planners/IdentityFacts.h"
#include "planners/ResolutionLevel.h"
#include "recipe/Efsh.h"
#include "recipe/Signals.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace BetterEnchantmentEffects {
class ActorEnvironment final : public SignalEnvironment {
public:
  ActorEnvironment(RE::Actor *a_actor, RE::MagicItem *a_enchantment,
                   std::optional<RecipeKey> a_effectKey = {});

  [[nodiscard]] float ActorValue(std::string_view a_name,
                                 Measure a_measure) const override;
  [[nodiscard]] float ActorState(ActorStateKind a_kind) const override;
  [[nodiscard]] Vec3 ActorVector(ActorStateKind a_kind) const override;
  [[nodiscard]] Vec3 WorldToRoot(const Vec3 &a_world) const override;
  [[nodiscard]] float Enchantment(EnchantmentField a_field) const override;
  [[nodiscard]] std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &a_record) const override;
  [[nodiscard]] std::optional<Efsh::EffectParams>
  MatchedEffectShader() const override;
  [[nodiscard]] const MatchedShaderFacts &MatchedShader() const noexcept;
  void SetAppliedResolution(ResolutionLevel a_level) noexcept;

private:
  [[nodiscard]] RE::NiPointer<RE::Actor> Actor() const noexcept;

  RE::ActorHandle actor_;
  RE::FormID enchantment_ = 0;
  std::optional<RecipeKey> effectKey_;
  std::optional<Efsh::EffectParams> matchedParams_;
  MatchedShaderFacts matchedShader_;
  ResolutionLevel appliedResolution_ = ResolutionLevel::kFull;
  mutable std::unordered_map<std::string, RE::ActorValue> actorValues_;
  mutable std::unordered_map<std::string, std::optional<Efsh::EffectParams>>
      shaders_;
};
}
