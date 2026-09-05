#pragma once

// The signal graph: every scalar or vector a recipe animates, compiled once
// per recipe and evaluated once per tick in dependency order. Engine-free;
// the wearer's state comes in through SignalEnvironment and firings through
// Fire(). The same graph types the expressions that masks use per texel.

#include "Expression.h"
#include "Recipe.h"
#include "Timing.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace WornEnchantmentPBR
{
	// What an event on the bus carries; a source fills what it has.
	struct TriggerPayload
	{
		std::string         node;
		std::string         arg;
		std::optional<Vec3> position;
		std::optional<Vec3> normal;
		float               value = 0.0f;
	};

	// One event as delivered: an id and its payload.
	struct EventRecord
	{
		std::string    id;
		TriggerPayload payload;
	};

	struct TriggerFiring
	{
		float          startTime = 0.0f;
		TriggerPayload payload;
	};

	// The wearer, as the engine (or a test) supplies it each tick.
	class SignalEnvironment
	{
	public:
		virtual ~SignalEnvironment() = default;
		[[nodiscard]] virtual float                               ActorValue(std::string_view a_name, Measure a_measure) const = 0;
		[[nodiscard]] virtual float                               ActorState(ActorStateKind a_kind) const = 0;
		[[nodiscard]] virtual float                               Enchantment(EnchantmentField a_field) const = 0;
		[[nodiscard]] virtual std::optional<Timing::EffectParams> EffectShader(const FormRef& a_record) const = 0;
	};

	// Everything zero: tests and recipes without actor input.
	class NullEnvironment final : public SignalEnvironment
	{
	public:
		float                               ActorValue(std::string_view, Measure) const override { return 0.0f; }
		float                               ActorState(ActorStateKind) const override { return 0.0f; }
		float                               Enchantment(EnchantmentField) const override { return 0.0f; }
		std::optional<Timing::EffectParams> EffectShader(const FormRef&) const override { return std::nullopt; }
	};

	struct TickInputs
	{
		float time = 0.0f;   // recipe seconds since apply, speed applied
		float delta = 0.0f;  // seconds since the previous tick, speed applied
	};

	// Compiled once per recipe. A node in a cycle, with an unknown reference,
	// a bad expression or a type mismatch is inert (0 or black) and reported
	// on its row, as is anything that reads it.
	class SignalGraph
	{
	public:
		[[nodiscard]] static SignalGraph Compile(std::span<const Signal> a_signals, std::span<const Curve> a_curves);

		[[nodiscard]] std::span<const Diagnostic>  Diagnostics() const noexcept { return diagnostics_; }
		[[nodiscard]] std::size_t                  Size() const noexcept { return nodes_.size(); }
		[[nodiscard]] std::optional<std::size_t>   Index(std::string_view a_name) const noexcept;
		[[nodiscard]] const Signal&                At(std::size_t a_index) const noexcept { return nodes_[a_index].signal; }
		[[nodiscard]] ValueType                    TypeOf(std::size_t a_index) const noexcept { return nodes_[a_index].type; }
		[[nodiscard]] std::optional<ValueType>     TypeOf(std::string_view a_name) const noexcept;
		[[nodiscard]] bool                         Inert(std::size_t a_index) const noexcept { return nodes_[a_index].inert; }
		// Evaluation order: every node after the nodes it reads.
		[[nodiscard]] std::span<const std::size_t> Order() const noexcept { return order_; }
		// A declared curve's program, by name.
		[[nodiscard]] const Program*               CurveProgram(std::string_view a_name) const noexcept;

	private:
		struct Node
		{
			Signal                     signal;
			ValueType                  type = ValueType::kScalar;
			std::vector<std::size_t>   deps;
			std::optional<Program>     expression;  // expr signals
			std::vector<std::uint32_t> exprRefs;    // node index per expression reference
			std::vector<const Program*> exprCurves;  // per expression curve call
			std::optional<Program>     curve;       // the row's curve, declared or inline
			bool                       inert = false;
		};
		std::vector<Node>                            nodes_;
		std::vector<std::size_t>                     order_;
		std::unordered_map<std::string, std::size_t> byName_;
		std::unordered_map<std::string, Program>     curves_;
		std::vector<Diagnostic>                      diagnostics_;
		friend class SignalState;
	};

	// Per applied recipe: phases, firings, counters, previous values and
	// the current value of every signal.
	class SignalState
	{
	public:
		explicit SignalState(const SignalGraph& a_graph);

		void Tick(const SignalEnvironment& a_environment, const TickInputs& a_inputs);
		// Delivers one event to every trigger whose source accepts it.
		void Fire(const EventRecord& a_event, float a_time);

		[[nodiscard]] Value ValueOf(std::size_t a_index) const noexcept;
		[[nodiscard]] Value ValueOf(std::string_view a_name) const noexcept;
		[[nodiscard]] float Scalar(std::string_view a_name) const noexcept;
		[[nodiscard]] Vec3  Vector(std::string_view a_name) const noexcept;
		[[nodiscard]] float Resolve(const Param& a_param) const noexcept;
		[[nodiscard]] Vec2  Resolve(const Vec2Param& a_param) const noexcept;
		[[nodiscard]] Vec3  Resolve(const Vec3Param& a_param) const noexcept;

		[[nodiscard]] std::span<const TriggerFiring> Firings(std::string_view a_trigger) const noexcept;

	private:
		struct NodeState
		{
			float                      phase = 0.0f;       // pulse
			std::vector<TriggerFiring> firings;            // trigger
			std::uint64_t              fired = 0;          // trigger: total firings ever
			std::uint64_t              seen = 0;           // counter, accumulate: firings consumed
			std::uint64_t              seenReset = 0;
			float                      accumulator = 0.0f;
			std::optional<Value>       previous;           // delta, smooth, when
			Value                      held = 0.0f;        // payload: last value seen
		};

		[[nodiscard]] float Scalar(std::size_t a_index) const noexcept;
		[[nodiscard]] Vec3  Vector(std::size_t a_index) const noexcept;
		[[nodiscard]] Value Evaluate(std::size_t a_index, const SignalEnvironment& a_environment, const TickInputs& a_inputs);
		void                Accept(std::size_t a_index, const EventRecord& a_event, float a_time);

		const SignalGraph&     graph_;
		std::vector<Value>     values_;
		std::vector<NodeState> states_;
	};
}
