// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
std::vector<RenderValueRef> InputsOf(const RenderStepKind &step) {
  return Match(
      step,
      [](const UnavailableStep &) { return std::vector<RenderValueRef>{}; },
      [](const ConstantRenderStep &) { return std::vector<RenderValueRef>{}; },
      [](const BuildBakeBuffersStep &k) {
        std::vector<RenderValueRef> inputs{k.mesh};
        if (const auto *origin = Get<RenderValueRef>(k.operation))
          inputs.push_back(*origin);
        return inputs;
      },
      [](const BakeMeshStep &k) { return std::vector{k.buffers}; },
      [](const NormalSlopeStep &k) { return std::vector{k.material}; },
      [](const SubmitMaterialSampleStep &k) { return std::vector{k.material}; },
      [](const ClusterMaterialStep &k) { return std::vector{k.sample}; },
      [](const DrawClustersStep &k) {
        return std::vector{k.material, k.analysis};
      },
      [](const SampleFieldStep &k) {
        auto inputs = k.coordinates;
        inputs.insert(inputs.begin(), k.texture);
        return inputs;
      },
      [](const SubmitReductionStep &k) { return std::vector{k.value}; },
      [](const BuildLookupStep &k) {
        std::vector<RenderValueRef> inputs;
        for (const auto &a : k.boundArguments)
          inputs.push_back(a.value);
        return inputs;
      },
      [](const EvaluateValueStep &k) { return k.inputs; },
      [](const EvaluateProgramStep &k) {
        auto inputs = k.inputs;
        inputs.insert(inputs.end(), k.lookups.begin(), k.lookups.end());
        return inputs;
      },
      [](const MapFieldStep &k) { return std::vector{k.value, k.lookup}; },
      [](const ComposeVectorStep &k) { return k.components; },
      [](const DrawRippleStep &k) {
        auto inputs = k.inputs;
        inputs.insert(inputs.begin(), k.positions);
        return inputs;
      },
      [](const CompositeStackStep &k) {
        std::vector<RenderValueRef> inputs{k.base, k.visibility};
        for (const auto &layer : k.layers) {
          inputs.push_back(layer.source);
          inputs.push_back(layer.opacity);
          if (layer.mask)
            inputs.push_back(*layer.mask);
          if (layer.color)
            inputs.push_back(*layer.color);
        }
        return inputs;
      });
}
RenderValueType OutputType(const RenderStepKind &step) {
  return Match(
      step, [](const UnavailableStep &k) -> RenderValueType { return k.type; },
      [](const EvaluateValueStep &k) -> RenderValueType { return k.type; },
      [](const ConstantRenderStep &k) -> RenderValueType {
        return TypeOf(k.value);
      },
      [](const BuildBakeBuffersStep &) -> RenderValueType {
        return RenderResourceType::kBakeBuffers;
      },
      [](const SubmitMaterialSampleStep &) -> RenderValueType {
        return RenderResourceType::kSubmission;
      },
      [](const ClusterMaterialStep &) -> RenderValueType {
        return RenderResourceType::kMaterialAnalysis;
      },
      [](const SubmitReductionStep &) -> RenderValueType {
        return RenderResourceType::kSubmission;
      },
      [](const BuildLookupStep &) -> RenderValueType {
        return RenderResourceType::kLookup;
      },
      [](const CompositeStackStep &) -> RenderValueType {
        return RenderResourceType::kStack;
      },
      [](const BakeMeshStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const NormalSlopeStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const DrawClustersStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const SampleFieldStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const EvaluateProgramStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const MapFieldStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const ComposeVectorStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      },
      [](const DrawRippleStep &) -> RenderValueType {
        return RenderResourceType::kTexture;
      });
}
std::string_view StepKindName(const RenderStepKind &step) {
  return Match(
      step,
      [](const UnavailableStep &) { return std::string_view{"Unavailable"}; },
      [](const ConstantRenderStep &) { return std::string_view{"Constant"}; },
      [](const BuildBakeBuffersStep &) {
        return std::string_view{"BuildBakeBuffers"};
      },
      [](const BakeMeshStep &) { return std::string_view{"BakeMesh"}; },
      [](const NormalSlopeStep &) { return std::string_view{"NormalSlope"}; },
      [](const SubmitMaterialSampleStep &) {
        return std::string_view{"SubmitMaterialSample"};
      },
      [](const ClusterMaterialStep &) {
        return std::string_view{"ClusterMaterial"};
      },
      [](const DrawClustersStep &) { return std::string_view{"DrawClusters"}; },
      [](const SampleFieldStep &) { return std::string_view{"SampleField"}; },
      [](const SubmitReductionStep &) {
        return std::string_view{"SubmitReduction"};
      },
      [](const BuildLookupStep &) { return std::string_view{"BuildLookup"}; },
      [](const EvaluateValueStep &) {
        return std::string_view{"EvaluateValue"};
      },
      [](const EvaluateProgramStep &) {
        return std::string_view{"EvaluateProgram"};
      },
      [](const MapFieldStep &) { return std::string_view{"MapField"}; },
      [](const ComposeVectorStep &) {
        return std::string_view{"ComposeVector"};
      },
      [](const DrawRippleStep &) { return std::string_view{"DrawRipple"}; },
      [](const CompositeStackStep &) {
        return std::string_view{"CompositeStack"};
      });
}
std::optional<TextureRequirements> RequirementsOf(const RenderStepKind &step) {
  return Match(step,
               [](const auto &kind) -> std::optional<TextureRequirements> {
                 if constexpr (requires { kind.requirements; })
                   return kind.requirements;
                 else
                   return std::nullopt;
               });
}
std::optional<RenderValueType> TypeOf(const RenderPlan &plan,
                                      RenderValueRef value) {
  return Match(
      value,
      [&](RenderInputRef ref) -> std::optional<RenderValueType> {
        return ref.input < plan.inputs.size()
                   ? std::optional{plan.inputs[ref.input].type}
                   : std::nullopt;
      },
      [&](StepOutputRef ref) -> std::optional<RenderValueType> {
        return ref.output == 0 && ref.step < plan.steps.size()
                   ? std::optional{OutputType(plan.steps[ref.step].kind)}
                   : std::nullopt;
      });
}
std::expected<void, std::string> ValidateRenderPlan(const RenderPlan &plan) {
  if (plan.steps.size() > 4096 || plan.inputs.size() > 8192)
    return std::unexpected("render plan exceeds its size limit");
  for (std::size_t i = 0; i < plan.steps.size(); ++i) {
    const auto &step = plan.steps[i];
    std::string problem;
    const auto require = [&](RenderValueRef ref, RenderValueType type) {
      const auto actual = TypeOf(plan, ref);
      if (!actual || *actual != type)
        problem = "incompatible operand type";
    };
    for (const auto &input : InputsOf(step.kind)) {
      if (!TypeOf(plan, input))
        problem = "invalid input reference";
      if (const auto *producer = Get<StepOutputRef>(input);
          producer && producer->step >= i)
        problem = "step dependencies are not in acyclic producer order";
    }
    Match(
        step.kind, [](const UnavailableStep &) {},
        [](const ConstantRenderStep &) {},
        [&](const BuildBakeBuffersStep &k) {
          require(k.mesh, RenderResourceType::kMesh);
          if (const auto *origin = Get<RenderValueRef>(k.operation))
            require(*origin, ValueType::kVec3);
        },
        [&](const BakeMeshStep &k) {
          require(k.buffers, RenderResourceType::kBakeBuffers);
        },
        [&](const NormalSlopeStep &k) {
          require(k.material, RenderResourceType::kMaterial);
        },
        [&](const SubmitMaterialSampleStep &k) {
          require(k.material, RenderResourceType::kMaterial);
        },
        [&](const ClusterMaterialStep &k) {
          require(k.sample, RenderResourceType::kMaterialSample);
        },
        [&](const DrawClustersStep &k) {
          require(k.material, RenderResourceType::kMaterial);
          require(k.analysis, RenderResourceType::kMaterialAnalysis);
        },
        [&](const SampleFieldStep &k) {
          const auto type = TypeOf(plan, k.texture);
          if (!type ||
              (*type != RenderValueType{RenderResourceType::kTexture} &&
               *type != RenderValueType{RenderResourceType::kMaterial}))
            problem = "field sampling requires a texture or material";
          for (auto coordinate : k.coordinates)
            require(coordinate, ValueType::kVec2);
          if (!k.field.graph || !k.field.graph->NodeAt(k.field.output.node))
            problem = "sample field has no valid graph output";
        },
        [&](const SubmitReductionStep &k) {
          require(k.value, RenderResourceType::kTexture);
          if (static_cast<unsigned>(k.kind) >
                  static_cast<unsigned>(ReductionKind::kMaximum) ||
              static_cast<unsigned>(k.type) >
                  static_cast<unsigned>(ValueType::kVec3))
            problem = "invalid reduction operation";
          if (!k.domain.width || !k.domain.height || k.domain.width > 4096 ||
              k.domain.height > 4096)
            problem = "invalid reduction domain";
        },
        [&](const BuildLookupStep &k) {
          const auto *function =
              k.graph ? k.graph->FunctionAt(k.function) : nullptr;
          if (!function || function->isDisabled || k.samples != 256 ||
              k.sampledParameter >= function->parameters.size()) {
            problem = "invalid lookup function or domain";
            return;
          }
          if (function->parameters[k.sampledParameter].type !=
              ValueType::kScalar)
            problem = "lookup sampled parameter must be scalar";
          if (function->result.node >= function->nodes.size() ||
              function->result.output >=
                  function->nodes[function->result.node].outputs.size() ||
              function->nodes[function->result.node]
                      .outputs[function->result.output]
                      .type != GraphValueType{ValueType::kScalar})
            problem = "lookup result must be scalar";
          std::vector<bool> bound(function->parameters.size());
          bound[k.sampledParameter] = true;
          for (const auto &arg : k.boundArguments) {
            if (arg.parameter >= bound.size() || bound[arg.parameter]) {
              problem = "duplicate or invalid lookup argument";
              continue;
            }
            bound[arg.parameter] = true;
            require(arg.value, function->parameters[arg.parameter].type);
          }
          if (std::ranges::find(bound, false) != bound.end())
            problem = "lookup argument is missing";
        },
        [&](const EvaluateValueStep &k) {
          const auto *node = k.value.graph
                                 ? k.value.graph->NodeAt(k.value.output.node)
                                 : nullptr;
          if (!node || k.value.graph->SampleDependent(k.value.output)) {
            problem = "invalid uniform operation";
            return;
          }
          const auto refs = InputsOf(node->kind);
          if (refs.size() != k.inputs.size()) {
            problem = "uniform bindings do not match";
            return;
          }
          for (std::size_t n = 0; n < refs.size(); ++n) {
            const auto type = k.value.graph->OutputType(refs[n]);
            const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
            if (!numeric)
              problem = "uniform operand is not numeric";
            else
              require(k.inputs[n], *numeric);
          }
        },
        [&](const EvaluateProgramStep &k) {
          if (k.inputs.size() != k.program.Inputs().size() ||
              k.lookups.size() != k.program.FunctionLookups().size()) {
            problem = "program bindings do not match";
            return;
          }
          for (std::size_t n = 0; n < k.inputs.size(); ++n) {
            const auto actual = TypeOf(plan, k.inputs[n]);
            if (Is<InterpreterTextureInput>(k.program.Inputs()[n]))
              require(k.inputs[n], RenderResourceType::kTexture);
            else if (!actual || !Is<ValueType>(*actual))
              problem = "program value input is not numeric";
          }
          for (auto lookup : k.lookups)
            require(lookup, RenderResourceType::kLookup);
        },
        [&](const MapFieldStep &k) {
          require(k.value, RenderResourceType::kTexture);
          require(k.lookup, RenderResourceType::kLookup);
        },
        [&](const ComposeVectorStep &k) {
          if (k.components.size() < 2 || k.components.size() > 3)
            problem = "invalid vector component count";
          for (auto component : k.components) {
            const auto type = TypeOf(plan, component);
            if (!type ||
                (*type != RenderValueType{RenderResourceType::kTexture} &&
                 *type != RenderValueType{ValueType::kScalar}))
              problem =
                  "vector component must be a scalar field or scalar value";
          }
        },
        [&](const DrawRippleStep &k) {
          require(k.positions, RenderResourceType::kTexture);
          if (!k.field.graph || !k.field.graph->NodeAt(k.field.output.node))
            problem = "invalid ripple field";
        },
        [&](const CompositeStackStep &k) {
          require(k.base, RenderResourceType::kStack);
          require(k.visibility, RenderResourceType::kVisibility);
          for (const auto &layer : k.layers) {
            const auto type = TypeOf(plan, layer.source);
            if (!type ||
                (*type != RenderValueType{RenderResourceType::kTexture} &&
                 *type != RenderValueType{ValueType::kVec3}))
              problem = "invalid stack source";
            require(layer.opacity, ValueType::kScalar);
            if (layer.mask)
              require(*layer.mask, RenderResourceType::kTexture);
            if (layer.color)
              require(*layer.color, ValueType::kVec3);
          }
        });
    if (!problem.empty())
      return std::unexpected(step.displayName + ": " + problem);
  }
  for (const auto &input : plan.inputs) {
    const auto *readback = Get<ReadbackBinding>(input.binding);
    if (!readback)
      continue;
    const auto *submission = readback->submission < plan.steps.size()
                                 ? &plan.steps[readback->submission].kind
                                 : nullptr;
    const auto *reduction =
        submission ? Get<SubmitReductionStep>(*submission) : nullptr;
    const bool valid =
        (reduction && input.type == RenderValueType{reduction->type}) ||
        (submission && Is<SubmitMaterialSampleStep>(*submission) &&
         input.type == RenderValueType{RenderResourceType::kMaterialSample});
    if (!valid)
      return std::unexpected("invalid readback input");
  }
  for (const auto &output : plan.stackOutputs)
    if (TypeOf(plan, output.result) !=
        std::optional<RenderValueType>{RenderResourceType::kStack})
      return std::unexpected("invalid stack output binding");
  for (const auto &use : plan.textureUses)
    if (TypeOf(plan, use.texture) !=
        std::optional<RenderValueType>{RenderResourceType::kTexture})
      return std::unexpected("invalid texture use binding");
  return {};
}
}
