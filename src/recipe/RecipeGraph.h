// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/GraphOperations.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeCompilation.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
struct OutputBinding {
  std::string property;
  OutputRef value;
};

class RecipeGraph {
public:
  [[nodiscard]] std::span<const OutputBinding> OutputBindings() const noexcept;
  [[nodiscard]] std::optional<OutputRef>
  FindSignalOutput(std::string_view a_name) const;
  [[nodiscard]] std::optional<NodeId>
  FindTrigger(std::string_view a_name) const;
  [[nodiscard]] const FunctionDefinition *
  FunctionAt(FunctionId a_function) const noexcept;
  [[nodiscard]] std::span<const FunctionDefinition> Functions() const noexcept;
  [[nodiscard]] std::span<const NodeId> TickOrder() const noexcept;
  [[nodiscard]] std::span<const NodeId> ChangingTickOrder() const noexcept;
  [[nodiscard]] bool SampleDependent(OutputRef a_output) const noexcept;
  [[nodiscard]] std::optional<GraphValueType>
  OutputType(OutputRef a_output) const noexcept;
  [[nodiscard]] std::string_view NameOf(NodeId a_node) const noexcept;
  [[nodiscard]] const Source *SourceAt(NodeId a_node) const noexcept;
  [[nodiscard]] bool IsMask(NodeId a_node) const noexcept;
  [[nodiscard]] const BoundExpression *
  ExpressionAt(NodeId a_node) const noexcept;
  [[nodiscard]] std::optional<FunctionId>
  FindFunction(std::string_view a_name) const;
  [[nodiscard]] std::optional<FunctionId>
  TransformFor(std::string_view a_location) const;
  [[nodiscard]] static RecipeGraph Compile(const Recipe &a_recipe);
  [[nodiscard]] std::span<const RecipeNode> Nodes() const noexcept;
  [[nodiscard]] const RecipeNode *NodeAt(std::size_t a_index) const noexcept;
  [[nodiscard]] const Signal *SignalAt(std::size_t a_index) const noexcept;
  [[nodiscard]] std::size_t Size() const noexcept;
  [[nodiscard]] std::optional<std::size_t>
  FindNodeIndex(std::string_view a_name) const;
  [[nodiscard]] std::optional<std::size_t>
  FindSignalIndex(std::string_view a_name) const;
  [[nodiscard]] std::optional<ValueType> TypeOf(std::string_view a_name) const;
  [[nodiscard]] std::optional<ValueType>
  TypeOf(std::size_t a_index) const noexcept;
  [[nodiscard]] bool IsDisabled(std::size_t a_index) const noexcept;
  [[nodiscard]] bool MayChangeOverTime(std::string_view a_name) const;
  [[nodiscard]] bool Changing(NodeId a_node) const noexcept;
  [[nodiscard]] std::span<const std::size_t> DependencyOrder() const noexcept;
  [[nodiscard]] std::span<const std::size_t>
  SignalEvaluationOrder() const noexcept;
  [[nodiscard]] const Program *
  ResultTransformFor(std::string_view a_diagnosticLocation) const;
  [[nodiscard]] std::span<const Diagnostic> Diagnostics() const noexcept;

private:
  std::vector<RecipeNode> nodes_;
  std::vector<OutputBinding> outputBindings_;
  std::vector<detail::RecipeDeclaration> declarations_;
  std::vector<FunctionDefinition> functions_;
  std::unordered_map<std::string, FunctionId> functionIndicesByName_;
  std::vector<NodeId> tickOrder_, changingTickOrder_;
  std::vector<bool> disabled_, sampleDependent_, changing_;
  std::unordered_map<NodeId, std::size_t> signalDeclarations_,
      sourceDeclarations_, maskDeclarations_;
  std::unordered_map<std::string, NodeId> triggersByName_;
  std::unordered_map<std::string, FunctionId> transformsByLocation_;
  bool lowered_ = false;
  std::unordered_map<std::string, std::size_t> nodeIndicesByName_;
  std::unordered_map<std::string, std::size_t> resultTransformsByLocation_;
  std::vector<std::size_t> dependencyOrder_;
  std::vector<std::size_t> signalEvaluationOrder_;
  std::vector<Diagnostic> diagnostics_;
  friend struct RecipeGraphBuilder;
  friend struct RecipeGraphLowering;
};
[[nodiscard]] Value EvaluateFunction(const RecipeGraph &a_graph,
                                     FunctionId a_function,
                                     std::span<const Value> a_arguments);
[[nodiscard]] bool IsAnimated(const RecipeGraph &a_graph,
                              const Output &a_output);

[[nodiscard]] bool ShareableAcrossActors(const Recipe &a_recipe,
                                         const RecipeGraph &a_graph,
                                         const Output &a_output);
[[nodiscard]] bool ShareableAcrossActors(const Recipe &a_recipe,
                                         const RecipeGraph &a_graph,
                                         const Mask &a_mask);

}
