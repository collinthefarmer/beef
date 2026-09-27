// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"

namespace BetterEnchantmentEffects {
using detail::DeclarationCategory;
using detail::DeclarationExpression;
using detail::RecipeDeclaration;
std::span<const OutputBinding> RecipeGraph::OutputBindings() const noexcept {
  return outputBindings_;
}
std::span<const RecipeNode> RecipeGraph::Nodes() const noexcept {
  return nodes_;
}
const RecipeNode *RecipeGraph::NodeAt(std::size_t index) const noexcept {
  return index < nodes_.size() ? &nodes_[index] : nullptr;
}
std::size_t RecipeGraph::Size() const noexcept {
  return lowered_ ? nodes_.size() : declarations_.size();
}
const Signal *RecipeGraph::SignalAt(std::size_t index) const noexcept {
  if (lowered_) {
    const auto it = signalDeclarations_.find(index);
    if (it == signalDeclarations_.end())
      return nullptr;
    index = it->second;
  }
  return index < declarations_.size()
             ? Get<Signal>(declarations_[index].definition)
             : nullptr;
}
std::optional<std::size_t>
RecipeGraph::FindNodeIndex(std::string_view name) const {
  const auto it = nodeIndicesByName_.find(std::string{name});
  return it == nodeIndicesByName_.end() ? std::nullopt
                                        : std::optional{it->second};
}
std::optional<std::size_t>
RecipeGraph::FindSignalIndex(std::string_view name) const {
  const auto index = FindNodeIndex(name);
  return index && SignalAt(*index) ? index : std::nullopt;
}
std::optional<OutputRef>
RecipeGraph::FindSignalOutput(std::string_view name) const {
  const auto index = FindSignalIndex(name);
  return index ? std::optional{OutputRef{*index, 0}} : std::nullopt;
}
std::optional<NodeId> RecipeGraph::FindTrigger(std::string_view name) const {
  const auto it = triggersByName_.find(std::string{name});
  return it == triggersByName_.end() ? std::nullopt : std::optional{it->second};
}
std::optional<ValueType> RecipeGraph::TypeOf(std::string_view name) const {
  const auto index = FindNodeIndex(name);
  return index ? TypeOf(*index) : std::nullopt;
}
std::optional<ValueType> RecipeGraph::TypeOf(std::size_t index) const noexcept {
  if (!lowered_)
    return index < declarations_.size()
               ? std::optional{declarations_[index].valueType}
               : std::nullopt;
  const auto type = OutputType({index, 0});
  const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
  return numeric ? std::optional{*numeric} : std::nullopt;
}
bool RecipeGraph::IsDisabled(std::size_t index) const noexcept {
  return lowered_
             ? index >= disabled_.size() || disabled_[index]
             : index >= declarations_.size() || declarations_[index].isDisabled;
}
bool RecipeGraph::MayChangeOverTime(std::string_view name) const {
  const auto index = FindNodeIndex(name);
  if (!index || IsDisabled(*index))
    return false;
  return lowered_ ? changing_[*index] : declarations_[*index].mayChangeOverTime;
}
bool RecipeGraph::SampleDependent(OutputRef output) const noexcept {
  return output.node < sampleDependent_.size() && sampleDependent_[output.node];
}
std::optional<GraphValueType>
RecipeGraph::OutputType(OutputRef output) const noexcept {
  const auto *node = NodeAt(output.node);
  return node && output.output < node->outputs.size()
             ? std::optional{node->outputs[output.output].type}
             : std::nullopt;
}
std::span<const std::size_t> RecipeGraph::DependencyOrder() const noexcept {
  return dependencyOrder_;
}
std::span<const std::size_t>
RecipeGraph::SignalEvaluationOrder() const noexcept {
  return signalEvaluationOrder_;
}
std::span<const NodeId> RecipeGraph::TickOrder() const noexcept {
  return tickOrder_;
}
std::span<const Diagnostic> RecipeGraph::Diagnostics() const noexcept {
  return diagnostics_;
}
const Program *
RecipeGraph::ResultTransformFor(std::string_view location) const {
  const auto it = resultTransformsByLocation_.find(std::string{location});
  if (it == resultTransformsByLocation_.end() ||
      it->second >= declarations_.size())
    return nullptr;
  const auto &declaration = declarations_[it->second];
  return !declaration.isDisabled && declaration.expression
             ? &declaration.expression->program
             : nullptr;
}
std::optional<FunctionId>
RecipeGraph::FindFunction(std::string_view name) const {
  const auto it = functionIndicesByName_.find(std::string{name});
  return it == functionIndicesByName_.end() ? std::nullopt
                                            : std::optional{it->second};
}
std::optional<FunctionId>
RecipeGraph::TransformFor(std::string_view location) const {
  const auto it = transformsByLocation_.find(std::string{location});
  return it == transformsByLocation_.end() ? std::nullopt
                                           : std::optional{it->second};
}
const FunctionDefinition *
RecipeGraph::FunctionAt(FunctionId id) const noexcept {
  return id < functions_.size() ? &functions_[id] : nullptr;
}
std::span<const FunctionDefinition> RecipeGraph::Functions() const noexcept {
  return functions_;
}
const Source *RecipeGraph::SourceAt(NodeId id) const noexcept {
  if (!lowered_)
    return id < declarations_.size() ? Get<Source>(declarations_[id].definition)
                                     : nullptr;
  const auto it = sourceDeclarations_.find(id);
  return it == sourceDeclarations_.end()
             ? nullptr
             : Get<Source>(declarations_[it->second].definition);
}
bool RecipeGraph::IsMask(NodeId id) const noexcept {
  return lowered_ ? maskDeclarations_.contains(id)
                  : id < declarations_.size() &&
                        Is<Mask>(declarations_[id].definition);
}
std::string_view RecipeGraph::NameOf(NodeId id) const noexcept {
  if (const auto *signal = SignalAt(id))
    return signal->name;
  if (const auto *source = SourceAt(id))
    return source->name;
  const auto it = maskDeclarations_.find(id);
  return it == maskDeclarations_.end() ? std::string_view{}
                                       : declarations_[it->second].name;
}
const BoundExpression *RecipeGraph::ExpressionAt(NodeId id) const noexcept {
  const auto *node = NodeAt(id);
  const auto *expression =
      node ? Get<ExpressionOperation>(node->kind) : nullptr;
  return expression ? &expression->expression : nullptr;
}
}
