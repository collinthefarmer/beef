// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "Settings.h"
#include "actors/Actor.h"
#include "recipe/Recipe.h"

#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct ObservedModel {
  RE::NiPointer<RE::NiAVObject> model;
  Actors::ModelToken token{};
  std::vector<Actors::GeometryFacts> geometries;
};

struct ModelTable {
  std::vector<ObservedModel> models;
};

struct ObserveRequest {
  RE::Actor &actor;
  const Settings &settings;
  bool effectsEnabled = false;
  std::span<const Recipe> recipes;
};

[[nodiscard]] RE::TESObjectARMO *PieceItemOf(const RE::BIPOBJECT &a_object);
[[nodiscard]] RE::MagicItem *WornEnchantment(RE::Actor *a_actor,
                                             RE::TESObjectARMO *a_armor);
[[nodiscard]] Actors::ActorFacts ObserveActor(const ObserveRequest &a_request,
                                              ModelTable &a_models);
void KeepModels(ModelTable &a_models, const Actors::ActorFacts &a_facts,
                const Actors::ActorPlan &a_installed);
}
