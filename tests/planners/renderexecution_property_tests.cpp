// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderExecution.h"
#include "test_support.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <tuple>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
using Fake = std::int64_t;
using FakeResult = std::expected<Fake, std::string>;
using Lookup = std::function<std::optional<Fake>(RenderValueRef)>;
using Content = std::function<std::optional<Fake>(std::size_t)>;
using Random = std::mt19937;
using RefKey = std::tuple<std::size_t, std::size_t, std::size_t>;
using Observed = std::map<RefKey, Fake>;
using ObservedVersions = std::map<RefKey, ChangeVersion>;

constexpr Fake kModulus = 1000003;
constexpr Fake kFailureDivisor = 13;
constexpr Fake kHandleSpan = Fake{1} << 32;
constexpr Fake kMaterialBase = 7;

enum class BaseImport { kHandleAndVersion, kHandleOnly };

struct GeneratedPlan {
  RenderPlan plan;
  std::vector<RenderInputId> scalars, textures, visibilities, bases;
  std::vector<StepOutputRef> stacks;
  std::vector<std::optional<std::size_t>> upstream;
  std::vector<RenderInputId> measurements;
};

Fake Handle(std::size_t step, ChangeVersion version) {
  return -(static_cast<Fake>(step + 1) * kHandleSpan +
           static_cast<Fake>(version % kHandleSpan));
}
bool IsHandle(Fake value) { return value < 0; }
std::size_t HandleStep(Fake value) {
  return static_cast<std::size_t>(-value / kHandleSpan - 1);
}

std::size_t Pick(Random &random, std::size_t count) {
  return std::uniform_int_distribution<std::size_t>{0, count - 1}(random);
}
RefKey KeyOf(RenderValueRef ref) {
  if (const auto *input = Get<RenderInputRef>(ref))
    return {0, input->input, 0};
  const auto &output = *Get<StepOutputRef>(ref);
  return {1, output.step, output.output};
}
std::optional<Fake> Find(const Observed &observed, RenderValueRef ref) {
  const auto found = observed.find(KeyOf(ref));
  return found == observed.end() ? std::nullopt : std::optional{found->second};
}
bool Chance(Random &random, double probability) {
  return std::bernoulli_distribution{probability}(random);
}

Fake Mix(Fake hash, Fake value) {
  return ((hash * 31 + value) % kModulus + kModulus) % kModulus;
}
FakeResult FailOrPass(Fake value) {
  if (value % kFailureDivisor == 0)
    return std::unexpected("fake failure");
  return value;
}
bool Hidden(Fake visibility, std::size_t layer) {
  return layer < 16 && ((visibility >> layer) & 1) != 0;
}
std::vector<RenderValueRef> VisibleOperands(const CompositeStackStep &stack,
                                            Fake visibility) {
  std::vector<RenderValueRef> refs;
  for (std::size_t i = 0; i < stack.layers.size(); ++i) {
    if (Hidden(visibility, i))
      continue;
    const auto &layer = stack.layers[i];
    std::ranges::copy(LayerOperands(stack, layer), std::back_inserter(refs));
  }
  if (!refs.empty())
    refs.push_back(stack.base);
  return refs;
}

FakeResult Compute(const RenderPlan &plan, std::size_t index,
                   const Lookup &value, const Content &content) {
  const auto &kind = plan.steps[index].kind;
  const auto hashOf = [&](std::span<const RenderValueRef> refs) -> FakeResult {
    Fake hash = static_cast<Fake>(index) + 1;
    for (const auto &ref : refs) {
      auto operand = value(ref);
      if (operand && IsHandle(*operand))
        operand = content(HandleStep(*operand));
      if (!operand)
        return std::unexpected("operand is missing");
      hash = Mix(hash, *operand);
    }
    return FailOrPass(hash);
  };
  return Match(
      kind,
      [](const UnavailableStep &k) -> FakeResult {
        return std::unexpected(k.problem);
      },
      [&](const ConstantRenderStep &) -> FakeResult {
        return static_cast<Fake>(index) * 10 + 1;
      },
      [&](const SubmitReductionStep &k) -> FakeResult {
        if (!value(k.value))
          return std::unexpected("operand is missing");
        return Fake{0};
      },
      [&](const CompositeStackStep &k) -> FakeResult {
        const auto visibility = value(k.visibility);
        if (!visibility)
          return std::unexpected("operand is missing");
        const auto refs = VisibleOperands(k, *visibility);
        if (refs.empty())
          return Fake{0};
        return hashOf(refs);
      },
      [&](const auto &) -> FakeResult { return hashOf(InputsOf(kind)); });
}

struct Reference {
  const RenderPlan &plan;
  const std::vector<std::optional<Fake>> &inputs;
  std::map<std::size_t, FakeResult> memo;
  std::set<std::size_t> required;

  FakeResult Evaluate(RenderValueRef ref) {
    if (const auto *input = Get<RenderInputRef>(ref)) {
      if (const auto *measurement =
              Get<ReadbackBinding>(plan.inputs[input->input].binding)) {
        const auto submitted =
            Evaluate(StepOutputRef{measurement->submission, 0});
        if (!submitted)
          return submitted;
      }
      if (!inputs[input->input])
        return std::unexpected("input is unset");
      return *inputs[input->input];
    }
    const auto step = Get<StepOutputRef>(ref)->step;
    if (const auto found = memo.find(step); found != memo.end())
      return found->second;
    required.insert(step);
    const auto result = EvaluateStep(step);
    memo.emplace(step, result);
    return result;
  }
  FakeResult EvaluateStep(std::size_t step) {
    std::vector<RenderValueRef> operands = InputsOf(plan.steps[step].kind);
    if (const auto *stack = Get<CompositeStackStep>(plan.steps[step].kind)) {
      const auto visibility = Evaluate(stack->visibility);
      if (!visibility)
        return visibility;
      operands = VisibleOperands(*stack, *visibility);
      operands.push_back(stack->visibility);
    }
    Observed values;
    for (const auto &operand : operands) {
      const auto result = Evaluate(operand);
      if (!result)
        return result;
      values.emplace(KeyOf(operand), *result);
    }
    return Compute(
        plan, step, [&](RenderValueRef ref) { return Find(values, ref); },
        [&](std::size_t upstream) -> std::optional<Fake> {
          const auto result = Evaluate(StepOutputRef{upstream, 0});
          return result ? std::optional{*result} : std::nullopt;
        });
  }
};

RenderValueRef Choose(Random &random, std::span<const RenderValueRef> refs) {
  return refs[Pick(random, refs.size())];
}

GeneratedPlan Generate(Random &random) {
  GeneratedPlan generated;
  auto &plan = generated.plan;
  std::vector<RenderValueRef> scalars, textures;
  const auto input =
      [&](RenderValueType type,
          decltype(RenderInput::binding) binding) -> RenderInputId {
    plan.inputs.push_back({type, binding});
    return plan.inputs.size() - 1;
  };
  const auto step = [&](RenderStepKind kind) -> StepOutputRef {
    plan.steps.push_back({std::move(kind), "step"});
    return StepOutputRef{plan.steps.size() - 1, 0};
  };
  for (std::size_t i = 0, n = 2 + Pick(random, 4); i < n; ++i) {
    generated.scalars.push_back(input(ValueType::kScalar, TextureValue{}));
    scalars.push_back(RenderInputRef{generated.scalars.back()});
  }
  for (std::size_t i = 0, n = 1 + Pick(random, 3); i < n; ++i) {
    generated.textures.push_back(
        input(RenderResourceType::kTexture, TextureValue{}));
    textures.push_back(RenderInputRef{generated.textures.back()});
  }
  scalars.push_back(step(ConstantRenderStep{1.0f}));
  if (Chance(random, 0.3))
    textures.push_back(
        step(UnavailableStep{RenderResourceType::kTexture, "unavailable"}));
  for (std::size_t i = 0, n = 3 + Pick(random, 8); i < n; ++i) {
    if (Chance(random, 0.25)) {
      const auto field = Choose(random, textures);
      const auto submission = step(SubmitReductionStep{
          ReductionKind::kMean, field, ValueType::kScalar, {64, 64}});
      generated.measurements.push_back(
          input(ValueType::kScalar, ReadbackBinding{submission.step}));
      scalars.push_back(RenderInputRef{generated.measurements.back()});
      continue;
    }
    ComposeVectorStep compose;
    for (std::size_t c = 0, width = 2 + Pick(random, 2); c < width; ++c)
      compose.components.push_back(Chance(random, 0.5)
                                       ? Choose(random, scalars)
                                       : Choose(random, textures));
    compose.requirements = {TextureSize{64}};
    textures.push_back(step(std::move(compose)));
  }
  for (std::size_t s = 0, n = 1 + Pick(random, 3); s < n; ++s) {
    const StackInputBinding binding{PlacementId{static_cast<std::uint32_t>(s)},
                                    0};
    generated.bases.push_back(input(RenderResourceType::kStack, binding));
    generated.visibilities.push_back(
        input(RenderResourceType::kVisibility, binding));
    CompositeStackStep stack{RenderInputRef{generated.bases.back()},
                             RenderInputRef{generated.visibilities.back()},
                             {},
                             {TextureSize{64}},
                             Slot::kEmissive};
    for (std::size_t l = 0, layers = 1 + Pick(random, 4); l < layers; ++l) {
      PlannedLayer layer{Choose(random, textures),
                         Choose(random, scalars),
                         std::nullopt,
                         std::nullopt,
                         Blend::kReplace,
                         ChannelSet{}};
      if (Chance(random, 0.4))
        layer.mask = Choose(random, textures);
      stack.layers.push_back(layer);
    }
    generated.stacks.push_back(step(std::move(stack)));
    const bool chainable = s > 0 && !generated.upstream[s - 1];
    generated.upstream.push_back(
        chainable && Chance(random, 0.5) ? std::optional{s - 1} : std::nullopt);
  }
  return generated;
}

struct Totals {
  std::size_t evaluations = 0, revisited = 0, redundant = 0, repeated = 0,
              unrequired = 0, unsound = 0, inexactOutputs = 0,
              inexactInputs = 0, releases = 0, restores = 0;
};
struct ReleasedOutput {
  Fake value = 0;
  ChangeVersion changeVersion = 0;
};

struct Harness {
  BaseImport baseImport;
  GeneratedPlan generated;
  RenderExecution<Fake> execution;
  std::vector<std::optional<Fake>> inputs;
  std::vector<std::size_t> executions;
  std::vector<std::optional<Observed>> lastObserved;
  std::vector<ObservedVersions> lastVersions;
  std::map<RenderStepId, std::vector<Fake>> submitted;
  std::map<RenderStepId, ReleasedOutput> released;
  Totals totals;
  Fake nextFresh = 1;

  Harness(BaseImport import, GeneratedPlan plan)
      : baseImport(import), generated(std::move(plan)),
        execution(generated.plan), inputs(generated.plan.inputs.size()),
        executions(generated.plan.steps.size()),
        lastObserved(generated.plan.steps.size()),
        lastVersions(generated.plan.steps.size()) {}

  void Set(RenderInputId id, Fake value) {
    const bool changed = !inputs[id] || *inputs[id] != value;
    const auto before = execution.Inputs()[id].changeVersion;
    Check(execution.SetInput(id, value, changed).has_value(), "set input");
    const bool advanced = execution.Inputs()[id].changeVersion != before;
    if (advanced != changed)
      ++totals.inexactInputs;
    inputs[id] = value;
  }
  std::optional<Fake> Evaluate(StepOutputRef output) {
    ++totals.evaluations;
    std::fill(executions.begin(), executions.end(), 0);
    std::vector<std::optional<Fake>> valuesBefore;
    std::vector<ChangeVersion> versionsBefore;
    for (const auto &state : execution.Steps()) {
      valuesBefore.push_back(state.outputs.front().value);
      versionsBefore.push_back(state.outputs.front().changeVersion);
    }
    const auto &plan = execution.Plan();
    const auto execute =
        [&](const RenderStep &step,
            std::span<const ResolvedRenderInput<Fake>> resolved,
            std::monostate &) -> FakeResult {
      const auto index = static_cast<std::size_t>(&step - plan.steps.data());
      ++executions[index];
      Observed observed;
      ObservedVersions versions;
      for (const auto &value : resolved) {
        observed.emplace(KeyOf(value.input), value.value);
        versions.emplace(KeyOf(value.input), value.changeVersion);
      }
      if (execution.Steps()[index].outputs.front().value &&
          lastObserved[index] && *lastObserved[index] == observed)
        ++(lastVersions[index] == versions ? totals.redundant
                                           : totals.revisited);
      auto result = Compute(
          plan, index, [&](RenderValueRef ref) { return Find(observed, ref); },
          [&](std::size_t upstream) {
            return execution.Steps()[upstream].outputs.front().value;
          });
      if (const auto *submission = Get<SubmitReductionStep>(step.kind);
          submission && result)
        if (const auto field = Find(observed, submission->value))
          submitted[index].push_back(*field / 3);
      if (result) {
        lastObserved[index] = observed;
        lastVersions[index] = versions;
      }
      return result;
    };
    const auto same = [](const Fake &a, const Fake &b) { return a == b; };
    const auto select = [](const CompositeStackStep &stack, const Fake &control)
        -> std::expected<std::vector<RenderValueRef>, std::string> {
      return VisibleOperands(stack, control);
    };
    const auto actual = execution.Evaluate(output, execute, same, select);
    Reference reference{plan, inputs, {}, {}};
    const auto expected = reference.Evaluate(output);
    if (actual.has_value() != expected.has_value() ||
        (actual && actual->value != *expected))
      ++totals.unsound;
    for (auto it = released.begin(); it != released.end();) {
      const auto &output = execution.Steps()[it->first].outputs.front();
      if (!output.value) {
        ++it;
        continue;
      }
      if (*output.value == it->second.value &&
          output.changeVersion != it->second.changeVersion)
        ++totals.inexactOutputs;
      if (*output.value != it->second.value &&
          output.changeVersion == it->second.changeVersion)
        ++totals.unsound;
      it = released.erase(it);
    }
    for (std::size_t i = 0; i < executions.size(); ++i) {
      if (executions[i] > 1)
        ++totals.repeated;
      if (executions[i] > 0 && !reference.required.contains(i))
        ++totals.unrequired;
      const auto &state = execution.Steps()[i].outputs.front();
      if (state.value && valuesBefore[i] &&
          state.changeVersion != versionsBefore[i] &&
          *state.value == *valuesBefore[i])
        ++totals.inexactOutputs;
    }
    return actual ? std::optional{actual->value} : std::nullopt;
  }
  void EvaluateChain(std::size_t stack) {
    const auto upstream = generated.upstream[stack];
    if (!upstream) {
      Evaluate(generated.stacks[stack]);
      return;
    }
    const auto upstreamStep = generated.stacks[*upstream].step;
    const auto content = Evaluate(generated.stacks[*upstream]);
    const auto version =
        execution.Steps()[upstreamStep].outputs.front().changeVersion;
    const bool drawn = content && *content != 0;
    Set(generated.bases[stack],
        !drawn
            ? kMaterialBase
            : Handle(upstreamStep, baseImport == BaseImport::kHandleAndVersion
                                       ? version
                                       : ChangeVersion{0}));
    Evaluate(generated.stacks[stack]);
  }
  std::uint64_t clock = 1;
  void Act(Random &random) {
    const auto &g = generated;
    execution.AdvanceClock(++clock);
    const auto roll = Pick(random, 13);
    if (roll < 3) {
      const auto id = g.scalars[Pick(random, g.scalars.size())];
      Set(id, nextFresh++);
    } else if (roll < 4) {
      const auto id = g.textures[Pick(random, g.textures.size())];
      Set(id, nextFresh++);
    } else if (roll < 5) {
      const auto id = Pick(random, inputs.size());
      if (inputs[id])
        Set(id, *inputs[id]);
    } else if (roll < 6) {
      const auto id = g.visibilities[Pick(random, g.visibilities.size())];
      Set(id, static_cast<Fake>(Pick(random, 16)));
    } else if (roll < 7) {
      const auto stack = Pick(random, g.stacks.size());
      if (!g.upstream[stack])
        Set(g.bases[stack], nextFresh++);
    } else if (roll < 8) {
      Release(Pick(random, 3));
    } else if (roll < 10 && !g.measurements.empty()) {
      Deliver(random, g.measurements[Pick(random, g.measurements.size())]);
    } else {
      EvaluateChain(Pick(random, g.stacks.size()));
    }
  }
  void Release(std::uint64_t idleTicks) {
    std::vector<std::optional<ReleasedOutput>> before;
    for (const auto &state : execution.Steps())
      before.push_back(state.outputs.front().value
                           ? std::optional{ReleasedOutput{
                                 *state.outputs.front().value,
                                 state.outputs.front().changeVersion}}
                           : std::nullopt);
    totals.releases += execution.ReleaseIdle(
        idleTicks, [](const RenderStep &step, const Fake &) {
          return !Is<CompositeStackStep>(step.kind) &&
                 !Is<SubmitReductionStep>(step.kind);
        });
    for (std::size_t i = 0; i < before.size(); ++i)
      if (before[i] && !execution.Steps()[i].outputs.front().value)
        released[i] = *before[i];
  }
  void Deliver(Random &random, RenderInputId measurement) {
    const auto &binding =
        *Get<ReadbackBinding>(generated.plan.inputs[measurement].binding);
    auto &pending = submitted[binding.submission];
    if (pending.empty())
      return;
    const auto completed = Pick(random, pending.size());
    Set(measurement, pending[completed]);
    pending.erase(pending.begin(), pending.begin() + completed + 1);
  }
  void Bind() {
    for (std::size_t id = 0; id < inputs.size(); ++id)
      if (!Get<ReadbackBinding>(generated.plan.inputs[id].binding))
        Set(id, std::ranges::find(generated.visibilities, id) !=
                        generated.visibilities.end()
                    ? Fake{0}
                    : nextFresh++);
  }
};

Totals Run(BaseImport baseImport) {
  Totals sum;
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    Random random{seed};
    auto generated = Generate(random);
    Check(ValidateRenderPlan(generated.plan).has_value(),
          "generated plan is valid");
    Harness harness{baseImport, std::move(generated)};
    harness.Bind();
    for (int action = 0; action < 400; ++action)
      harness.Act(random);
    auto &t = harness.totals;
    t.restores = harness.execution.Restores();
    sum.evaluations += t.evaluations;
    sum.redundant += t.redundant;
    sum.revisited += t.revisited;
    sum.repeated += t.repeated;
    sum.unrequired += t.unrequired;
    sum.unsound += t.unsound;
    sum.inexactOutputs += t.inexactOutputs;
    sum.inexactInputs += t.inexactInputs;
    sum.releases += t.releases;
    sum.restores += t.restores;
  }
  return sum;
}
}

int main() {
  const auto totals = Run(BaseImport::kHandleAndVersion);
  Check(totals.evaluations > 10000, "the generator exercises the executor");
  test::Equal(
      totals.unsound, std::size_t{0},
      "soundness: every cached result equals a from-scratch evaluation");
  test::Equal(
      totals.inexactInputs, std::size_t{0},
      "exact versioning: an input version advances only on a new value");
  test::Equal(
      totals.inexactOutputs, std::size_t{0},
      "exact versioning: an output version advances only on a new value");
  test::Equal(totals.repeated, std::size_t{0},
              "minimality: a step executes at most once per evaluation");
  test::Equal(totals.unrequired, std::size_t{0},
              "minimality: only steps the requested output reads execute");
  test::Equal(totals.redundant, std::size_t{0},
              "minimality: a cached step reruns only when an observed value "
              "changed");
  Check(totals.releases > 1000 && totals.restores > 100,
        "the generator releases idle steps and restores them on demand");
  const auto handleOnly = Run(BaseImport::kHandleOnly);
  Check(handleOnly.unsound > 0,
        "the soundness check detects a chained base imported without its "
        "upstream content version");
  std::printf("render execution properties: %zu reruns after a value "
              "returned to its observed value; %zu stale results without "
              "the base content version; %zu releases, %zu restores\n",
              totals.revisited, handleOnly.unsound, totals.releases,
              totals.restores);
  return test::Finish("render execution properties");
}
