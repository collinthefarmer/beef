// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ValueIdentity.h"

#include <bit>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::size_t kMaxIdentityTextBytes = std::size_t{1024} * 1024;
struct Encoding {
  std::string text;
  void Add(std::string_view value) {
    text += std::to_string(value.size()) + ":";
    text += value;
  }
  template <class T> void Number(T value) {
    if constexpr (std::is_floating_point_v<T>)
      Add(std::to_string(std::bit_cast<std::uint32_t>(value)));
    else
      Add(std::to_string(static_cast<std::uint64_t>(value)));
  }
  void Vector(const Value &value) {
    Number(value.index());
    const Vec3 v = AsVec3(value);
    Number(v.x);
    Number(v.y);
    Number(v.z);
  }
};
struct IdentityScope {
  std::span<const RecipeNode> nodes;
  std::size_t depth = 0;
  bool global = false;
};
void EncodeOutputType(Encoding &out, const RecipeNode &node, OutputRef ref) {
  if (ref.output >= node.outputs.size())
    return;
  const GraphValueType &type = node.outputs[ref.output].type;
  out.Number(type.index());
  Match(
      type, [&](ValueType t) { out.Number(t); },
      [&](ResourceType t) { out.Number(t); });
}
void EncodeBakeOperation(Encoding &out, const BakeOperation &operation) {
  out.Number(operation.bake.index());
  Match(
      operation.bake, [&](const PartitionBake &b) { out.Number(b.bipedSlot); },
      [&](const BoneWeightBake &b) {
        out.Number(b.bones.size());
        for (const auto &bone : b.bones)
          out.Add(bone);
      },
      [](const PositionBake &) {}, [](const LocalPositionBake &) {},
      [](const NormalBake &) {}, [](const UvBake &) {},
      [](const ComponentIdBake &) {}, [](const ChartIdBake &) {});
}
void EncodeMaterialClustersOperation(
    Encoding &out, const MaterialClustersOperation &operation) {
  const ClusterSettings &s = operation.settings;
  out.Number(s.clusters);
  out.Number(s.seed);
  out.Number(s.iterations);
  out.Number(s.weights.roughness);
  out.Number(s.weights.metallic);
  out.Number(s.weights.occlusion);
  out.Number(s.weights.reflectance);
  out.Number(s.weights.luma);
  out.Number(s.weights.color);
}
void EncodeSettings(Encoding &out, const NodeKind &kind) {
  Match(
      kind, [&](const ConstantOperation &k) { out.Vector(k.value); },
      [](const ExternalInput &) {}, [](const ExpressionOperation &) {},
      [&](const ParameterOperation &k) { out.Number(k.parameter); },
      [](const MapFunctionOperation &) {}, [](const CallOperation &) {},
      [&](const WaveOperation &k) { out.Number(k.waveform); },
      [&](const EffectShaderOperation &k) { out.Number(k.field); },
      [&](const NoiseOperation &k) { out.Number(k.seed); },
      [&](const GradientOperation &k) {
        for (const auto &stop : k.stops)
          out.Number(stop.at);
      },
      [&](const TriggerOperation &k) {
        out.Number(k.origin.index());
        out.Number(k.max);
        out.Number(k.payloadType);
        out.Number(k.anchor.index());
        if (const auto *anchor = Get<NodeAnchor>(k.anchor))
          out.Add(anchor->node);
      },
      [&](const CounterOperation &k) {
        out.Number(k.reset.has_value());
        out.Number(k.cap.has_value());
      },
      [&](const TextureCoordinatesOperation &k) {
        out.Number(k.scroll.has_value());
        out.Number(k.tile.has_value());
        out.Number(k.mirror[0]);
        out.Number(k.mirror[1]);
        out.Number(k.transpose);
      },
      [&](const ImageOperation &k) {
        out.Number(k.channel);
        out.Number(k.space);
        out.Number(k.mip);
      },
      [&](const MaterialOperation &k) { out.Number(k.channel); },
      [&](const BakeOperation &k) { EncodeBakeOperation(out, k); },
      [&](const RippleOperation &k) { out.Number(k.shape); },
      [&](const MaterialClustersOperation &k) {
        EncodeMaterialClustersOperation(out, k);
      },
      [](const VectorOperation &) {}, [](const RampOperation &) {},
      [](const ToRootOperation &) {}, [](const HoldOperation &) {},
      [](const AccumulateOperation &) {}, [](const RateOperation &) {},
      [](const SmoothOperation &) {}, [](const DistanceOperation &) {},
      [&](const ReductionOperation &k) { out.Number(k.kind); });
}
struct IdentityBuilder {
  const RecipeGraph &graph;
  const ValueBindings &bindings;
  std::size_t visits = 0;

  std::expected<std::string, std::string> Node(const IdentityScope &scope,
                                               OutputRef ref) {
    if (++visits > 65536)
      return std::unexpected("value identity exceeds its work limit");
    if (scope.depth > 64)
      return std::unexpected("value dependencies exceed 64 levels");
    const RecipeNode *node = IdentifiableNode(scope, ref);
    if (!node)
      return std::unexpected("invalid or disabled value in texture demand");
    Encoding out;
    out.Number(node->kind.index());
    out.Number(ref.output);
    EncodeOutputType(out, *node, ref);
    EncodeSettings(out, node->kind);
    if (const std::expected<void, std::string> references =
            EncodeReferences(out, node->kind, scope.depth);
        !references)
      return std::unexpected(node->displayName + ": " + references.error());
    if (Stateful(node->kind))
      if (const std::expected<void, std::string> state =
              EncodeState(out, scope, ref.node);
          !state)
        return std::unexpected(state.error());
    if (out.text.size() > kMaxIdentityTextBytes)
      return std::unexpected("value identity exceeds its size limit");
    if (const std::expected<void, std::string> inputs =
            EncodeInputs(out, scope, *node);
        !inputs)
      return std::unexpected(inputs.error());
    return out.text;
  }

  [[nodiscard]] const RecipeNode *IdentifiableNode(const IdentityScope &scope,
                                                   OutputRef ref) const {
    if (ref.node >= scope.nodes.size() ||
        ref.output >= scope.nodes[ref.node].outputs.size() ||
        (scope.global && graph.IsDisabled(ref.node)))
      return nullptr;
    return &scope.nodes[ref.node];
  }

  std::expected<void, std::string>
  EncodeReferences(Encoding &out, const NodeKind &kind, std::size_t depth) {
    return Match(
        kind,
        [&](const ExternalInput &k) -> std::expected<void, std::string> {
          return EncodeExternalInput(out, k);
        },
        [&](const ExpressionOperation &k) -> std::expected<void, std::string> {
          return EncodeExpressionOperation(out, k, depth);
        },
        [&](const MapFunctionOperation &k) -> std::expected<void, std::string> {
          return EncodeFunction(out, k.function, depth);
        },
        [&](const CallOperation &k) -> std::expected<void, std::string> {
          return EncodeFunction(out, k.function, depth);
        },
        [](const auto &) -> std::expected<void, std::string> { return {}; });
  }

  std::expected<void, std::string> EncodeExternalInput(Encoding &out,
                                                       const ExternalInput &k) {
    out.Number(k.source.index());
    if (Is<SampleUvInput>(k.source))
      return {};
    if (!bindings.external)
      return std::unexpected("external input has no bound identity");
    const std::expected<std::string, std::string> identity =
        bindings.external(k.source);
    if (!identity)
      return std::unexpected(identity.error());
    out.Add(*identity);
    return {};
  }

  std::expected<void, std::string>
  EncodeExpressionOperation(Encoding &out, const ExpressionOperation &k,
                            std::size_t depth) {
    out.Number(k.expression.program.OpCount());
    for (const Program::Node &instruction : k.expression.program.Code()) {
      out.Number(instruction.op);
      if (instruction.op == Program::Op::kNumber)
        out.Number(instruction.number);
      if (instruction.op == Program::Op::kRef)
        out.Number(instruction.index);
      if (instruction.op != Program::Op::kCurve)
        continue;
      if (instruction.index >= k.expression.functionBindings.size())
        return std::unexpected("invalid function binding");
      const BoundFunction &binding =
          k.expression.functionBindings[instruction.index];
      if (const std::expected<void, std::string> function =
              EncodeFunction(out, binding.function, depth);
          !function)
        return function;
      out.Number(binding.sampledParameter);
      for (const BoundFunctionArgument &argument : binding.arguments)
        out.Number(argument.parameter);
    }
    return {};
  }

  std::expected<void, std::string> EncodeFunction(Encoding &out, FunctionId id,
                                                  std::size_t depth) {
    const FunctionDefinition *function = graph.FunctionAt(id);
    if (!function || function->isDisabled)
      return std::unexpected("invalid function in value identity");
    Encoding encoded;
    encoded.Number(function->parameters.size());
    for (const FunctionParameter &parameter : function->parameters)
      encoded.Number(parameter.type);
    const std::expected<std::string, std::string> body = Node(
        IdentityScope{function->nodes, depth + 1, false}, function->result);
    if (!body)
      return std::unexpected(body.error());
    encoded.Add(*body);
    out.Add(encoded.text);
    return {};
  }

  std::expected<void, std::string>
  EncodeState(Encoding &out, const IdentityScope &scope, NodeId node) {
    if (!scope.global || !bindings.state)
      return std::unexpected("state owner has no bound identity");
    out.Add(bindings.state(node));
    return {};
  }

  std::expected<void, std::string> EncodeInputs(Encoding &out,
                                                const IdentityScope &scope,
                                                const RecipeNode &node) {
    const std::vector<OutputRef> inputs = InputsOf(node.kind);
    out.Number(inputs.size());
    const IdentityScope inputScope{scope.nodes, scope.depth + 1, scope.global};
    for (const OutputRef input : inputs) {
      const std::expected<std::string, std::string> identity =
          Node(inputScope, input);
      if (!identity)
        return std::unexpected(identity.error());
      if (out.text.size() + identity->size() > kMaxIdentityTextBytes)
        return std::unexpected("value identity exceeds its size limit");
      out.Add(*identity);
    }
    return {};
  }
};
}
std::expected<ValueIdentity, std::string>
IdentifyValue(const RecipeGraph &graph, OutputRef value,
              const ValueBindings &bindings) {
  IdentityBuilder builder{graph, bindings};
  std::expected<std::string, std::string> result =
      builder.Node(IdentityScope{graph.Nodes(), 0, true}, value);
  if (!result)
    return std::unexpected(result.error());
  return ValueIdentity{std::move(*result)};
}
}
