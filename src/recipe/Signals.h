#pragma once

#include "recipe/Efsh.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BetterEnchantmentEffects
{
	struct TriggerPayload
	{
		std::string         node;
		std::string         arg;
		std::optional<Vec3> position;
		std::optional<Vec3> normal;
		float               value = 0.0f;
	};

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

	class SignalEnvironment
	{
	public:
		virtual ~SignalEnvironment() = default;
		[[nodiscard]] virtual float                             ActorValue(std::string_view a_name, Measure a_measure) const = 0;
		[[nodiscard]] virtual float                             ActorState(ActorStateKind a_kind) const = 0;
		[[nodiscard]] virtual float                             Enchantment(EnchantmentField a_field) const = 0;
		[[nodiscard]] virtual std::optional<Efsh::EffectParams> EffectShader(const FormRef& a_record) const = 0;
	};

	class NullEnvironment final : public SignalEnvironment
	{
	public:
		float                             ActorValue(std::string_view, Measure) const override { return 0.0f; }
		float                             ActorState(ActorStateKind) const override { return 0.0f; }
		float                             Enchantment(EnchantmentField) const override { return 0.0f; }
		std::optional<Efsh::EffectParams> EffectShader(const FormRef&) const override { return std::nullopt; }
	};

	struct TickInputs
	{
		float time = 0.0f;
		float delta = 0.0f;
	};

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
		[[nodiscard]] std::span<const std::size_t> Order() const noexcept { return order_; }
		[[nodiscard]] const Program*               CurveProgram(std::string_view a_name) const noexcept;

	private:
		struct Node
		{
			Signal                      signal;
			ValueType                   type = ValueType::kScalar;
			std::vector<std::size_t>    deps;
			std::optional<Program>      expression;
			std::vector<std::uint32_t>  exprRefs;
			std::vector<const Program*> exprCurves;
			std::optional<Program>      curve;
			bool                        inert = false;
		};
		std::vector<Node>                            nodes_;
		std::vector<std::size_t>                     order_;
		std::unordered_map<std::string, std::size_t> byName_;
		std::unordered_map<std::string, Program>     curves_;
		std::vector<Diagnostic>                      diagnostics_;

		static void ReportSignal(SignalGraph& a_graph, std::string_view a_name, std::string a_message);
		static void ParseCurves(SignalGraph& a_graph, std::span<const Curve> a_curves);
		static void RegisterNodes(SignalGraph& a_graph, std::span<const Signal> a_signals);
		static void ResolveRefs(SignalGraph& a_graph);
		static void OrderNodes(SignalGraph& a_graph);
		static void InferTypes(SignalGraph& a_graph);
		static void CheckReferenceTypes(SignalGraph& a_graph);
		static void PropagateInert(SignalGraph& a_graph);

		friend class SignalState;
	};

	struct RowTypes
	{
		const Recipe&      recipe;
		const SignalGraph& graph;
	};

	[[nodiscard]] std::optional<ValueType> SignalTypeOf(const RowTypes& a_rows, std::string_view a_name) noexcept;
	[[nodiscard]] std::optional<ValueType> TexelTypeOf(const RowTypes& a_rows, std::string_view a_name, std::size_t a_depth = 0);
	[[nodiscard]] std::optional<ValueType> MaskTypeOf(const RowTypes& a_rows, const Mask& a_mask, std::size_t a_depth = 0);
	[[nodiscard]] bool                     NamesTrigger(const RowTypes& a_rows, std::string_view a_name) noexcept;

	[[nodiscard]] std::vector<Diagnostic> CheckCurve(const RowTypes& a_rows, const Curve& a_curve);
	[[nodiscard]] std::vector<Diagnostic> CheckSource(const RowTypes& a_rows, const Source& a_source);
	[[nodiscard]] std::vector<Diagnostic> CheckMask(const RowTypes& a_rows, const Mask& a_mask);
	[[nodiscard]] std::vector<Diagnostic> CheckLayer(const RowTypes& a_rows, const Layer& a_layer, Slot a_slot, std::string_view a_where);
	[[nodiscard]] std::vector<Diagnostic> CheckOutput(const RowTypes& a_rows, const Output& a_output, std::string_view a_where);

	class SignalState
	{
	public:
		explicit SignalState(const SignalGraph& a_graph);

		void Tick(const SignalEnvironment& a_environment, const TickInputs& a_inputs);
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
			float                      phase = 0.0f;
			std::vector<TriggerFiring> firings;
			std::uint64_t              fired = 0;
			std::uint64_t              seen = 0;
			std::uint64_t              seenReset = 0;
			float                      accumulator = 0.0f;
			std::optional<Value>       previous;
			Value                      held = 0.0f;
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
