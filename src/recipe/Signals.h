// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Efsh.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeGraph.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
struct TriggerPayload {
  Value value = 0.0f;
  std::string node;
  std::string arg;
};

struct CarriedPoint {
  Vec3 position;
};
struct AnchorNode {
  std::string_view node;
};
using FiringAnchor = std::variant<std::monostate, CarriedPoint, AnchorNode>;
[[nodiscard]] FiringAnchor AnchorOf(const TriggerSignal &a_trigger,
                                    const TriggerPayload &a_payload) noexcept;

struct EventRecord {
  std::string id;
  TriggerPayload payload;
  bool plugin = false;
};

struct TriggerFiring {
  float startTime = 0.0f;
  TriggerPayload payload;
};

class SignalEnvironment {
public:
  virtual ~SignalEnvironment() = default;
  [[nodiscard]] virtual float ActorValue(std::string_view a_name,
                                         Measure a_measure) const = 0;
  [[nodiscard]] virtual float ActorState(ActorStateKind a_kind) const = 0;
  [[nodiscard]] virtual Vec3 ActorVector(ActorStateKind a_kind) const = 0;
  [[nodiscard]] virtual Vec3 WorldToRoot(const Vec3 &a_world) const = 0;
  [[nodiscard]] virtual float Enchantment(EnchantmentField a_field) const = 0;
  [[nodiscard]] virtual std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &a_record) const = 0;
};

class NullEnvironment final : public SignalEnvironment {
public:
  float ActorValue(std::string_view, Measure) const override { return 0.0f; }
  float ActorState(ActorStateKind) const override { return 0.0f; }
  Vec3 ActorVector(ActorStateKind) const override { return Vec3{}; }
  Vec3 WorldToRoot(const Vec3 &a_world) const override { return a_world; }
  float Enchantment(EnchantmentField) const override { return 0.0f; }
  std::optional<Efsh::EffectParams>
  EffectShader(const FormRef &) const override {
    return std::nullopt;
  }
};

struct TickInputs {
  float time = 0.0f;
  float delta = 0.0f;
};

struct RowTypes {
  const Recipe &recipe;
  const RecipeGraph &graph;
};

[[nodiscard]] std::optional<ValueType>
SignalTypeOf(const RowTypes &a_rows, std::string_view a_name) noexcept;
[[nodiscard]] std::optional<ValueType> TexelTypeOf(const RowTypes &a_rows,
                                                   std::string_view a_name);
[[nodiscard]] std::optional<ValueType> MaskTypeOf(const RowTypes &a_rows,
                                                  const Mask &a_mask);
[[nodiscard]] bool NamesTrigger(const RowTypes &a_rows,
                                std::string_view a_name) noexcept;

[[nodiscard]] std::vector<Diagnostic> CheckCurve(const RowTypes &a_rows,
                                                 const Curve &a_curve);
[[nodiscard]] std::vector<Diagnostic> CheckSourceInputs(const RowTypes &a_rows,
                                                        const Source &a_source);
[[nodiscard]] std::vector<Diagnostic> CheckSource(const RowTypes &a_rows,
                                                  const Source &a_source);
[[nodiscard]] std::vector<Diagnostic> CheckMask(const RowTypes &a_rows,
                                                const Mask &a_mask);
[[nodiscard]] std::vector<Diagnostic> CheckLayer(const RowTypes &a_rows,
                                                 const Layer &a_layer,
                                                 Slot a_slot,
                                                 std::string_view a_where);
[[nodiscard]] std::vector<Diagnostic> CheckOutput(const RowTypes &a_rows,
                                                  const Output &a_output,
                                                  std::string_view a_where);

class SignalState {
public:
  explicit SignalState(const RecipeGraph &a_graph);

  void Tick(const SignalEnvironment &a_environment, const TickInputs &a_inputs);
  void Fire(const EventRecord &a_event, float a_time);

  [[nodiscard]] Value ValueOf(OutputRef a_output) const noexcept;
  [[nodiscard]] Value ValueOf(std::size_t a_index) const noexcept;
  [[nodiscard]] Value ValueOf(std::string_view a_name) const noexcept;
  [[nodiscard]] float Scalar(std::string_view a_name) const noexcept;
  [[nodiscard]] Vec3 Vector(std::string_view a_name) const noexcept;
  [[nodiscard]] float Resolve(const Param &a_param) const noexcept;
  [[nodiscard]] Vec2 Resolve(const Vec2Param &a_param) const noexcept;
  [[nodiscard]] Vec3 Resolve(const Vec3Param &a_param) const noexcept;

  [[nodiscard]] std::span<const TriggerFiring>
  Firings(std::string_view a_trigger) const noexcept;
  [[nodiscard]] std::span<const TriggerFiring>
  Firings(OutputRef a_output) const noexcept;
  [[nodiscard]] std::uint64_t AcceptedCount(OutputRef a_output) const noexcept;
  [[nodiscard]] std::uint64_t
  Mismatched(std::string_view a_trigger) const noexcept;
  [[nodiscard]] FiringAnchor
  AnchorOf(std::string_view a_trigger,
           const TriggerFiring &a_firing) const noexcept;

private:
  struct Evaluator;
  struct WaveState {
    float phase = 0.0f;
  };
  struct TriggerState {
    void RecordFiring(TriggerFiring a_firing, std::uint32_t a_limit);
    std::vector<TriggerFiring> firings;
    std::uint64_t accepted = 0;
    std::uint64_t mismatched = 0;
    float previousCondition = 0.0f;
  };
  struct HoldState {
    Value held = 0.0f;
  };
  struct CounterState {
    std::uint64_t seen = 0;
    std::uint64_t seenReset = 0;
    float value = 0.0f;
  };
  struct AccumulateState {
    std::uint64_t seen = 0;
    float value = 0.0f;
  };
  struct RateState {
    std::optional<Value> previous;
  };
  struct SmoothState {
    std::optional<Value> previous;
  };
  using OperationState =
      std::variant<WaveState, TriggerState, HoldState, CounterState,
                   AccumulateState, RateState, SmoothState>;

  template <class State> [[nodiscard]] State *Memory(NodeId a_node) noexcept;
  template <class State>
  [[nodiscard]] const State *Memory(NodeId a_node) const noexcept;

  [[nodiscard]] Value Evaluate(NodeId a_node,
                               const SignalEnvironment &a_environment,
                               const TickInputs &a_inputs);
  [[nodiscard]] Value DefaultValue(NodeId a_node) const noexcept;
  void Accept(NodeId a_node, const EventRecord &a_event, float a_time);
  void Store(OutputRef a_output, Value a_value);

  const RecipeGraph &graph_;
  std::vector<std::size_t> outputOffsets_;
  std::vector<Value> values_;
  std::vector<std::optional<std::size_t>> stateSlots_;
  std::vector<OperationState> states_;
  std::vector<NodeId> triggers_;
  bool ticked_ = false;
};
}
