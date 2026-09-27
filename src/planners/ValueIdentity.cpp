// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ValueIdentity.h"

#include <bit>

namespace BetterEnchantmentEffects {
namespace {
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
struct IdentityBuilder {
  const RecipeGraph &graph;
  const ValueBindings &bindings;
  std::size_t visits = 0;

  std::expected<std::string, std::string> Function(FunctionId id,
                                                   std::size_t depth) {
    const auto *function = graph.FunctionAt(id);
    if (!function || function->isDisabled)
      return std::unexpected("invalid function in value identity");
    Encoding out;
    out.Number(function->parameters.size());
    for (const auto &parameter : function->parameters)
      out.Number(parameter.type);
    auto body = Node(function->nodes, function->result, depth + 1, false);
    if (!body)
      return std::unexpected(body.error());
    out.Add(*body);
    return out.text;
  }

  std::expected<std::string, std::string>
  Node(std::span<const RecipeNode> nodes, OutputRef ref, std::size_t depth,
       bool global) {
    if (++visits > 65536)
      return std::unexpected("value identity exceeds its work limit");
    if (depth > 64)
      return std::unexpected("value dependencies exceed 64 levels");
    if (ref.node >= nodes.size() ||
        ref.output >= nodes[ref.node].outputs.size() ||
        (global && graph.IsDisabled(ref.node)))
      return std::unexpected("invalid or disabled value in texture demand");
    const auto &node = nodes[ref.node];
    Encoding out;
    out.Number(node.kind.index());
    out.Number(ref.output);
    const auto &type = node.outputs[ref.output].type;
    out.Number(type.index());
    Match(
        type, [&](ValueType t) { out.Number(t); },
        [&](ResourceType t) { out.Number(t); });
    std::string problem;
    const auto function = [&](FunctionId id) {
      auto identity = Function(id, depth);
      if (identity)
        out.Add(*identity);
      else
        problem = identity.error();
    };
    Match(
        node.kind, [&](const ConstantOperation &k) { out.Vector(k.value); },
        [&](const ExternalInput &k) {
          out.Number(k.source.index());
          if (Is<SampleUvInput>(k.source))
            return;
          if (!bindings.external) {
            problem = "external input has no bound identity";
            return;
          }
          auto identity = bindings.external(k.source);
          if (identity)
            out.Add(*identity);
          else
            problem = identity.error();
        },
        [&](const ExpressionOperation &k) {
          out.Number(k.expression.program.OpCount());
          for (const auto &instruction : k.expression.program.Code()) {
            out.Number(instruction.op);
            if (instruction.op == Program::Op::kNumber)
              out.Number(instruction.number);
            if (instruction.op == Program::Op::kRef)
              out.Number(instruction.index);
            if (instruction.op == Program::Op::kCurve) {
              if (instruction.index >= k.expression.functionBindings.size())
                problem = "invalid function binding";
              else {
                const auto &binding =
                    k.expression.functionBindings[instruction.index];
                function(binding.function);
                out.Number(binding.sampledParameter);
                for (const auto &argument : binding.arguments)
                  out.Number(argument.parameter);
              }
            }
          }
        },
        [&](const ParameterOperation &k) { out.Number(k.parameter); },
        [&](const MapFunctionOperation &k) { function(k.function); },
        [&](const CallOperation &k) { function(k.function); },
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
        [&](const BakeOperation &k) {
          out.Number(k.bake.index());
          Match(
              k.bake, [&](const PartitionBake &b) { out.Number(b.bipedSlot); },
              [&](const BoneWeightBake &b) {
                out.Number(b.bones.size());
                for (const auto &bone : b.bones)
                  out.Add(bone);
              },
              [](const PositionBake &) {}, [](const LocalPositionBake &) {},
              [](const NormalBake &) {}, [](const UvBake &) {},
              [](const ComponentIdBake &) {}, [](const ChartIdBake &) {});
        },
        [&](const RippleOperation &k) { out.Number(k.shape); },
        [&](const MaterialClustersOperation &k) {
          const auto &s = k.settings;
          out.Number(s.clusters);
          out.Number(s.seed);
          out.Number(s.iterations);
          out.Number(s.weights.roughness);
          out.Number(s.weights.metallic);
          out.Number(s.weights.occlusion);
          out.Number(s.weights.reflectance);
          out.Number(s.weights.luma);
          out.Number(s.weights.color);
        },
        [](const VectorOperation &) {}, [](const RampOperation &) {},
        [](const ToRootOperation &) {}, [](const HoldOperation &) {},
        [](const AccumulateOperation &) {}, [](const RateOperation &) {},
        [](const SmoothOperation &) {}, [](const DistanceOperation &) {},
        [&](const ReductionOperation &k) { out.Number(k.kind); });
    if (!problem.empty())
      return std::unexpected(node.displayName + ": " + problem);
    if (Stateful(node.kind)) {
      if (!global || !bindings.state)
        return std::unexpected("state owner has no bound identity");
      out.Add(bindings.state(ref.node));
    }
    if (out.text.size() > 1024 * 1024)
      return std::unexpected("value identity exceeds its size limit");
    const auto inputs = InputsOf(node.kind);
    out.Number(inputs.size());
    for (const auto input : inputs) {
      auto identity = Node(nodes, input, depth + 1, global);
      if (!identity)
        return identity;
      if (out.text.size() + identity->size() > 1024 * 1024)
        return std::unexpected("value identity exceeds its size limit");
      out.Add(*identity);
    }
    return out.text;
  }
};
}
std::expected<ValueIdentity, std::string>
IdentifyValue(const RecipeGraph &graph, OutputRef value,
              const ValueBindings &bindings) {
  IdentityBuilder builder{graph, bindings};
  auto result = builder.Node(graph.Nodes(), value, 0, true);
  if (!result)
    return std::unexpected(result.error());
  return ValueIdentity{std::move(*result)};
}
}
