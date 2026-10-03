// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorMatches.h"
#include "recipe/Recipe.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
enum class ActorId : std::uint32_t {};
enum class ModelToken : std::uint64_t {};

struct PieceRef {
  FormKey item;
  std::optional<FormKey> addon;
  bool firstPerson = false;
  [[nodiscard]] bool operator==(const PieceRef &) const = default;
};

struct GeometryRef {
  PieceRef piece;
  std::string name;
  std::uint32_t ordinal = 0;
  [[nodiscard]] bool operator==(const GeometryRef &) const = default;
};

struct InstanceRef {
  std::string recipe;
  std::uint64_t fingerprint = 0;
  std::optional<FormKey> enchantment;
  std::optional<RecipeKey> effectKey;
  [[nodiscard]] bool operator==(const InstanceRef &) const = default;
};

struct PlacementRef {
  InstanceRef instance;
  GeometryRef geometry;
  [[nodiscard]] bool operator==(const PlacementRef &) const = default;
};

struct GeometryFacts {
  std::string name;
  std::string diffusePath;
};

struct PieceFacts {
  PieceRef ref;
  std::uint32_t itemID = 0;
  WornPiece keys;
  std::optional<ModelToken> model;
  std::vector<GeometryFacts> geometries;
};

struct ActorFacts {
  ActorId actor{};
  bool loaded = false;
  std::optional<float> distance;
  std::vector<PieceFacts> pieces;
};

struct PlannedInstance {
  InstanceRef ref;
  [[nodiscard]] bool operator==(const PlannedInstance &) const = default;
};

struct PlannedGeometry {
  GeometryRef ref;
  ModelToken model{};
  [[nodiscard]] bool operator==(const PlannedGeometry &) const = default;
};

struct PlannedPlacement {
  PlacementRef ref;
  RecipeKey matchedBy;
  std::vector<OutputPlacement> outputs;
  int priority = 0;
  [[nodiscard]] bool operator==(const PlannedPlacement &) const = default;
};

struct ActorPlan {
  std::vector<PlannedInstance> instances;
  std::vector<PlannedGeometry> geometries;
  std::vector<PlannedPlacement> placements;
  [[nodiscard]] bool operator==(const ActorPlan &) const = default;
};

struct PendingModel {
  PieceRef piece;
  std::uint32_t sinceMs = 0;
  [[nodiscard]] bool operator==(const PendingModel &) const = default;
};

struct Unloaded {
  [[nodiscard]] bool operator==(const Unloaded &) const = default;
};
struct Evicted {
  [[nodiscard]] bool operator==(const Evicted &) const = default;
};
struct Present {
  ActorPlan installed;
  std::vector<PendingModel> pending;
  [[nodiscard]] bool operator==(const Present &) const = default;
};
using Phase = std::variant<Unloaded, Evicted, Present>;

struct InstallInstance {
  PlannedInstance instance;
};
struct RetireInstance {
  InstanceRef ref;
};
struct InstallGeometry {
  PlannedGeometry geometry;
};
struct RetireGeometry {
  GeometryRef ref;
};
struct InstallPlacement {
  PlannedPlacement placement;
};
struct RetirePlacement {
  PlacementRef ref;
};
struct RebuildRender {};
using ActorCommand =
    std::variant<InstallInstance, RetireInstance, InstallGeometry,
                 RetireGeometry, InstallPlacement, RetirePlacement,
                 RebuildRender>;

struct Advanced {
  Phase phase;
  std::vector<ActorCommand> commands;
};

[[nodiscard]] const PlannedInstance *Find(const ActorPlan &a_plan,
                                          const InstanceRef &a_ref) noexcept;
[[nodiscard]] const PlannedGeometry *Find(const ActorPlan &a_plan,
                                          const GeometryRef &a_ref) noexcept;
[[nodiscard]] const PlannedPlacement *Find(const ActorPlan &a_plan,
                                           const PlacementRef &a_ref) noexcept;
}
