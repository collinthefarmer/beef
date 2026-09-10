#pragma once

#include "recipe/Merge.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
enum class PieceId : std::size_t {};
enum class InstanceId : std::size_t {};
enum class PlacementId : std::size_t {};
enum class RecipeId : std::size_t {};
enum class OutputIndex : std::size_t {};

struct Piece {
  GeometryIdentity identity;
  WornPiece keys;
  bool firstPerson = false;
  bool lost = false;
};

struct Instance {
  RecipeId recipe{};
  std::optional<FormKey> enchantment;
  int priority = 0;
  [[nodiscard]] bool operator==(const Instance &) const = default;
};

struct OutputPlacement {
  OutputIndex output{};
  bool selected = false;
  std::string problem;
  [[nodiscard]] bool operator==(const OutputPlacement &) const = default;
};

struct Placement {
  InstanceId instance{};
  PieceId piece{};
  RecipeKey key;
  std::vector<OutputPlacement> outputs;
  [[nodiscard]] bool operator==(const Placement &) const = default;
};

struct ActorState {
  std::vector<Piece> pieces;
  std::vector<Instance> instances;
  std::vector<Placement> placements;
};

[[nodiscard]] const Piece *PieceAt(const ActorState &a_state,
                                   PieceId a_piece) noexcept;
[[nodiscard]] const Instance *InstanceAt(const ActorState &a_state,
                                         InstanceId a_instance) noexcept;
[[nodiscard]] const Placement *PlacementAt(const ActorState &a_state,
                                           PlacementId a_placement) noexcept;

[[nodiscard]] bool AnyLivePiece(const ActorState &a_state) noexcept;
[[nodiscard]] std::optional<InstanceId>
FindInstance(const ActorState &a_state, RecipeId a_recipe,
             const std::optional<FormKey> &a_enchantment) noexcept;
[[nodiscard]] std::vector<PlacementId>
PlacementsOfPiece(const ActorState &a_state, PieceId a_piece);
[[nodiscard]] std::vector<PlacementId>
PlacementsOfInstance(const ActorState &a_state, InstanceId a_instance);

struct SignalProjection {
  std::string name;
  SignalKindId kind = SignalKindId::kConstant;
  ValueType type = ValueType::kScalar;
  Value value = 0.0f;
  bool inert = false;
  std::string problem;
  std::optional<SignalKind> definition;
  std::optional<Value> constant;
  std::string text;
  std::string event;
  std::string curve;
};

struct ScalarProjection {
  ScalarField field = ScalarField::kStrength;
  float value = 0.0f;
  std::string text;
  [[nodiscard]] bool operator==(const ScalarProjection &) const = default;
};

struct LayerProjection {
  std::string source;
  std::string mask;
  std::string blend;
  float opacity = 0.0f;
  std::string opacityText;
  std::string color;
  std::string curve;
  std::string channels;
  [[nodiscard]] bool operator==(const LayerProjection &) const = default;
};

struct OutputProjection {
  OutputIndex output{};
  Target target = Target::kMaterial;
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  bool replace = false;
  std::optional<std::size_t> chain;
  std::vector<ScalarProjection> scalars;
  std::vector<LayerProjection> layers;
};

[[nodiscard]] std::vector<SignalProjection>
ProjectSignals(const Recipe &a_recipe, const SignalGraph &a_graph,
               const SignalState &a_signals);
[[nodiscard]] std::vector<ScalarProjection>
ProjectScalars(const SurfaceOutput &a_output, const SignalState &a_signals);
[[nodiscard]] std::vector<LayerProjection>
ProjectLayers(const SurfaceOutput &a_output, const SignalState &a_signals);
[[nodiscard]] OutputProjection
ProjectOutput(const Recipe &a_recipe, OutputIndex a_output,
              const SignalState &a_signals, std::optional<std::size_t> a_chain);
}
