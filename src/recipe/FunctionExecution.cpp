// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"

#include <array>

namespace BetterEnchantmentEffects {
namespace {
Value Zero(ValueType type) {
  switch (type) {
  case ValueType::kVec2:
    return Vec2{};
  case ValueType::kVec3:
    return Vec3{};
  case ValueType::kScalar:
    return 0.0f;
  }
  return 0.0f;
}

Value FunctionDefault(const FunctionDefinition &function) {
  if (function.result.node >= function.nodes.size())
    return 0.0f;
  const auto &node = function.nodes[function.result.node];
  if (function.result.output >= node.outputs.size())
    return 0.0f;
  const auto *type = Get<ValueType>(node.outputs[function.result.output].type);
  return type ? Zero(*type) : Value{0.0f};
}

Value ExecuteFunction(const RecipeGraph &graph, FunctionId id,
                      std::span<const Value> arguments, std::size_t depth);

struct FunctionExecutor {
  const RecipeGraph &graph;
  std::span<const Value> parameters;
  std::span<const Value> evaluated;
  std::size_t depth;

  Value Read(OutputRef reference) const {
    return reference.output == 0 && reference.node < evaluated.size()
               ? evaluated[reference.node]
               : Value{0.0f};
  }

  Value operator()(const ConstantOperation &operation) const {
    return operation.value;
  }

  Value operator()(const ParameterOperation &operation) const {
    return operation.parameter < parameters.size()
               ? parameters[operation.parameter]
               : Value{0.0f};
  }

  Value operator()(const VectorOperation &operation) const {
    if (operation.components.size() == 2)
      return Vec2{AsScalar(Read(operation.components[0])),
                  AsScalar(Read(operation.components[1]))};
    if (operation.components.size() == 3)
      return Vec3{AsScalar(Read(operation.components[0])),
                  AsScalar(Read(operation.components[1])),
                  AsScalar(Read(operation.components[2]))};
    return 0.0f;
  }

  Value operator()(const ExpressionOperation &operation) const {
    const auto &expression = operation.expression;
    if (expression.valueBindings.size() > kMaxExpressionOps)
      return 0.0f;
    std::array<Value, kMaxExpressionOps> values;
    for (std::size_t i = 0; i < expression.valueBindings.size(); ++i)
      values[i] = Read(expression.valueBindings[i]);
    Program::Inputs inputs;
    inputs.refs = std::span{values.data(), expression.valueBindings.size()};
    inputs.callFunction = [&](std::size_t binding, float x, float) {
      if (binding >= expression.functionBindings.size())
        return 0.0f;
      const auto &call = expression.functionBindings[binding];
      const auto *function = graph.FunctionAt(call.function);
      if (!function || function->parameters.size() > kMaxExpressionOps ||
          call.sampledParameter >= function->parameters.size())
        return 0.0f;
      std::vector<Value> arguments(function->parameters.size(), Value{0.0f});
      arguments[call.sampledParameter] = x;
      for (const auto &argument : call.arguments) {
        if (argument.parameter >= arguments.size())
          return 0.0f;
        arguments[argument.parameter] = Read(argument.value);
      }
      return AsScalar(
          ExecuteFunction(graph, call.function, arguments, depth + 1));
    };
    return expression.program.Evaluate(inputs);
  }

  Value operator()(const CallOperation &operation) const {
    if (operation.arguments.size() > kMaxExpressionOps)
      return 0.0f;
    std::array<Value, kMaxExpressionOps> arguments;
    for (std::size_t i = 0; i < operation.arguments.size(); ++i)
      arguments[i] = Read(operation.arguments[i]);
    return ExecuteFunction(
        graph, operation.function,
        std::span{arguments.data(), operation.arguments.size()}, depth + 1);
  }

  Value operator()(const MapFunctionOperation &operation) const {
    return (*this)(CallOperation{operation.function, operation.arguments});
  }

  template <class Operation> Value operator()(const Operation &) const {
    return 0.0f;
  }
};

Value ExecuteFunction(const RecipeGraph &graph, FunctionId id,
                      std::span<const Value> arguments, std::size_t depth) {
  const auto *function = graph.FunctionAt(id);
  if (!function)
    return 0.0f;
  if (function->isDisabled || depth >= kMaxExpressionDepth ||
      function->nodes.size() > kMaxExpressionOps ||
      function->parameters.size() != arguments.size())
    return FunctionDefault(*function);
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (TypeOf(arguments[i]) != function->parameters[i].type)
      return FunctionDefault(*function);
  }
  std::array<Value, kMaxExpressionOps> values;
  for (std::size_t i = 0; i < function->nodes.size(); ++i) {
    const auto &node = function->nodes[i];
    const auto *type = node.outputs.size() == 1
                           ? Get<ValueType>(node.outputs.front().type)
                           : nullptr;
    if (!type)
      return FunctionDefault(*function);
    const Value value =
        Match(node.kind, FunctionExecutor{graph, arguments,
                                          std::span{values.data(), i}, depth});
    values[i] = TypeOf(value) == *type ? value : Zero(*type);
  }
  return function->result.output == 0 &&
                 function->result.node < function->nodes.size()
             ? values[function->result.node]
             : FunctionDefault(*function);
}
}

Value EvaluateFunction(const RecipeGraph &a_graph, FunctionId a_function,
                       std::span<const Value> a_arguments) {
  return ExecuteFunction(a_graph, a_function, a_arguments, 0);
}
}
