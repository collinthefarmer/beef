#include "engine/Environment.h"

#include "engine/EngineForms.h"

namespace BetterEnchantmentEffects {
ActorEnvironment::ActorEnvironment(RE::Actor *a_actor,
                                   RE::MagicItem *a_enchantment)
    : actor_(a_actor ? a_actor->GetHandle() : RE::ActorHandle{}),
      enchantment_(a_enchantment ? a_enchantment->GetFormID() : 0) {}

RE::NiPointer<RE::Actor> ActorEnvironment::Actor() const noexcept {
  return actor_.get();
}

float ActorEnvironment::ActorValue(std::string_view a_name,
                                   Measure a_measure) const {
  const auto actor = Actor();
  if (!actor) {
    return 0.0f;
  }
  auto it = actorValues_.find(std::string{a_name});
  if (it == actorValues_.end()) {
    const auto *list = RE::ActorValueList::GetSingleton();
    const auto value =
        list ? list->LookupActorValueByName(a_name) : RE::ActorValue::kNone;
    it = actorValues_.emplace(std::string{a_name}, value).first;
  }
  const auto value = it->second;
  if (value == RE::ActorValue::kNone) {
    return 0.0f;
  }
  auto *owner = actor->AsActorValueOwner();
  if (!owner) {
    return 0.0f;
  }
  using Modifier = RE::ACTOR_VALUE_MODIFIERS::ACTOR_VALUE_MODIFIER;
  switch (a_measure) {
  case Measure::kCurrent:
    return owner->GetActorValue(value);
  case Measure::kBase:
    return owner->GetBaseActorValue(value);
  case Measure::kPermanent:
    return owner->GetPermanentActorValue(value);
  case Measure::kTemporaryModifier:
    return actor->GetActorValueModifier(Modifier::kTemporary, value);
  case Measure::kDamage:
    return -actor->GetActorValueModifier(Modifier::kDamage, value);
  case Measure::kMax:
    return owner->GetPermanentActorValue(value) +
           actor->GetActorValueModifier(Modifier::kTemporary, value);
  }
  return 0.0f;
}

float ActorEnvironment::ActorState(ActorStateKind a_kind) const {
  const auto actor = Actor();
  if (!actor) {
    return 0.0f;
  }
  switch (a_kind) {
  case ActorStateKind::kInCombat:
    return actor->IsInCombat() ? 1.0f : 0.0f;
  case ActorStateKind::kSneaking:
    return actor->IsSneaking() ? 1.0f : 0.0f;
  case ActorStateKind::kWeaponDrawn: {
    const auto *state = actor->AsActorState();
    return state && state->IsWeaponDrawn() ? 1.0f : 0.0f;
  }
  case ActorStateKind::kSwimming:
    return actor->AsActorState() && actor->AsActorState()->IsSwimming() ? 1.0f
                                                                        : 0.0f;
  case ActorStateKind::kSprinting:
    return actor->AsActorState() && actor->AsActorState()->IsSprinting() ? 1.0f
                                                                         : 0.0f;
  case ActorStateKind::kMounted:
    return actor->IsOnMount() ? 1.0f : 0.0f;
  case ActorStateKind::kMovementSpeed:
    return actor->AsActorState() ? actor->AsActorState()->DoGetMovementSpeed()
                                 : 0.0f;
  case ActorStateKind::kHasTarget:
    return actor->GetActorRuntimeData().currentCombatTarget.get() ? 1.0f : 0.0f;
  case ActorStateKind::kPosition:
  case ActorStateKind::kTarget:
    return 0.0f;
  }
  return 0.0f;
}

Vec3 ActorEnvironment::ActorVector(ActorStateKind a_kind) const {
  const auto actor = Actor();
  if (!actor) {
    return Vec3{};
  }
  switch (a_kind) {
  case ActorStateKind::kPosition: {
    const RE::NiPoint3 p = actor->GetPosition();
    return Vec3{p.x, p.y, p.z};
  }
  case ActorStateKind::kTarget: {
    const auto target = actor->GetActorRuntimeData().currentCombatTarget.get();
    if (!target) {
      return Vec3{};
    }
    const RE::NiPoint3 p = target->GetPosition();
    return Vec3{p.x, p.y, p.z};
  }
  default:
    return Vec3{};
  }
}

Vec3 ActorEnvironment::WorldToRoot(const Vec3 &a_world) const {
  const auto actor = Actor();
  RE::NiAVObject *root = actor ? actor->Get3D(false) : nullptr;
  if (!root) {
    return a_world;
  }
  const RE::NiPoint3 local =
      root->world.Invert() * RE::NiPoint3{a_world.x, a_world.y, a_world.z};
  return Vec3{local.x, local.y, local.z};
}

float ActorEnvironment::Enchantment(EnchantmentField a_field) const {
  const auto *item = enchantment_
                         ? RE::TESForm::LookupByID<RE::MagicItem>(enchantment_)
                         : nullptr;
  const auto *effect = item ? item->GetCostliestEffectItem() : nullptr;
  if (!effect) {
    return 0.0f;
  }
  switch (a_field) {
  case EnchantmentField::kMagnitude:
    return effect->GetMagnitude();
  case EnchantmentField::kCost:
    return effect->cost;
  }
  return 0.0f;
}

std::optional<Efsh::EffectParams>
ActorEnvironment::EffectShader(const FormRef &a_record) const {
  if (const auto it = shaders_.find(a_record.text); it != shaders_.end()) {
    return it->second;
  }
  std::optional<Efsh::EffectParams> params;
  if (a_record.key) {
    if (const auto *shader = LookupForm<RE::TESEffectShader>(*a_record.key)) {
      params = RecordFrom(*shader).params;
    }
  }
  shaders_.emplace(a_record.text, params);
  return params;
}
}
