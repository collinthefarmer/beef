// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "Settings.h"
#include "actors/Actor.h"
#include "recipe/Recipe.h"

#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
enum class MaterialLayout { kUnchecked, kVerified, kFailed };

struct ObservedGeometry {
  Actors::GeometryFacts facts;
  RE::NiPointer<RE::BSGeometry> geometry;
};

struct ObservedModel {
  RE::NiPointer<RE::NiAVObject> model;
  Actors::ModelToken token{};
  std::vector<ObservedGeometry> geometries;
  bool settled = false;
  std::uint32_t lastSeenMs = 0;
};

struct ModelTable {
  std::vector<ObservedModel> models;
};

struct ObserveRequest {
  RE::Actor &actor;
  const Settings &settings;
  bool effectsEnabled = false;
  std::span<const Recipe> recipes;
  std::uint32_t nowMs = 0;
};

[[nodiscard]] Actors::ActorFacts ObserveActor(const ObserveRequest &a_request,
                                              ModelTable &a_models,
                                              MaterialLayout &a_layout);
[[nodiscard]] const ObservedModel *ModelOf(const ModelTable &a_models,
                                           Actors::ModelToken a_token) noexcept;
void KeepModels(ModelTable &a_models, const Actors::ActorPlan &a_installed,
                std::uint32_t a_nowMs);
}
