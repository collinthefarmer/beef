// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"
#include "recipe/Signals.h"

#include <algorithm>
#include <format>
#include <limits>

namespace BetterEnchantmentEffects {
using detail::DeclarationCategory;
using detail::DeclarationExpression;
using detail::RecipeDeclaration;
void LowerRecipeGraph(RecipeGraph &a_graph, const Recipe &a_recipe);
namespace {
void AddRef(std::vector<std::string> &a_out, std::string_view a_name) {
  if (!a_name.empty() && !std::ranges::contains(a_out, a_name)) {
    a_out.emplace_back(a_name);
  }
}

void AddRef(std::vector<std::string> &a_out, const Param &a_param) {
  if (const auto *ref = Get<Ref>(a_param)) {
    AddRef(a_out, ref->name);
  }
}

template <std::size_t N>
void AddRef(std::vector<std::string> &a_out,
            const std::variant<std::array<Param, N>, Ref> &a_param) {
  Match(
      a_param, [&](const Ref &r) { AddRef(a_out, r.name); },
      [&](const std::array<Param, N> &parts) {
        for (const auto &p : parts) {
          AddRef(a_out, p);
        }
      });
}

std::vector<std::string> Dependencies(const Signal &a_signal,
                                      const Program *a_expression) {
  std::vector<std::string> out;
  Match(
      a_signal.kind,
      [&](const WaveSignal &k) {
        AddRef(out, k.base);
        AddRef(out, k.amplitude);
        AddRef(out, k.period);
        AddRef(out, k.phase);
      },
      [&](const RampSignal &k) {
        AddRef(out, k.from);
        AddRef(out, k.to);
        AddRef(out, k.seconds);
      },
      [&](const TriggerSignal &k) {
        AddRef(out, k.lifetime);
        if (const auto *when = Get<WhenOrigin>(k.origin)) {
          AddRef(out, when->when.name);
          if (when->value) {
            AddRef(out, when->value->name);
          }
        }
      },
      [&](const PayloadSignal &k) { AddRef(out, k.trigger.name); },
      [&](const CounterSignal &k) {
        AddRef(out, k.trigger.name);
        if (k.reset)
          AddRef(out, k.reset->name);
        if (k.cap)
          AddRef(out, *k.cap);
      },
      [&](const AccumulateSignal &k) {
        AddRef(out, k.trigger.name);
        AddRef(out, k.decay);
      },
      [&](const NoiseSignal &k) {
        AddRef(out, k.frequency);
        AddRef(out, k.amplitude);
      },
      [&](const GradientSignal &k) {
        AddRef(out, k.t);
        for (const auto &s : k.stops) {
          AddRef(out, s.color);
        }
      },
      [&](const RateSignal &k) { AddRef(out, k.of.name); },
      [&](const ToRootSignal &k) { AddRef(out, k.of.name); },
      [&](const SmoothSignal &k) {
        AddRef(out, k.of.name);
        AddRef(out, k.seconds);
      },
      [&](const ExprSignal &) {
        if (a_expression) {
          for (const auto &r : a_expression->References()) {
            AddRef(out, r);
          }
        }
      },
      [](const ConstantSignal &) {}, [](const EfshSignal &) {},
      [](const ActorValueSignal &) {}, [](const ActorStateSignal &) {},
      [](const EnchantmentSignal &) {});
  return out;
}

std::vector<std::string> Dependencies(const Source &a_source) {
  std::vector<std::string> out;
  if (const auto *image = Get<ImageSource>(a_source.kind)) {
    if (image->scroll)
      AddRef(out, *image->scroll);
    if (image->tile)
      AddRef(out, *image->tile);
  }
  if (const auto *ripple = Get<RippleSource>(a_source.kind)) {
    AddRef(out, ripple->trigger.name);
    AddRef(out, ripple->speed);
    AddRef(out, ripple->width);
    AddRef(out, ripple->decay);
    AddRef(out, ripple->direction);
  }
  return out;
}

bool SignalAnimated(const SignalKind &kind) {
  return !Is<ConstantSignal>(kind) && !Is<ExprSignal>(kind) &&
         !Is<GradientSignal>(kind) && !Is<RateSignal>(kind) &&
         !Is<SmoothSignal>(kind);
}
}

struct RecipeGraphBuilder {
  RecipeGraph graph;

  static void ReportSignal(RecipeGraph &a_graph, std::string_view a_name,
                           std::string a_message) {
    Reporter{a_graph.diagnostics_, SignalWhere(a_name)}.Error(
        std::move(a_message));
  }

  void Reject(RecipeDeclaration &node, std::string message) {
    node.isDisabled = true;
    Reporter{graph.diagnostics_, node.displayName}.Error(std::move(message));
  }

  template <class Definition>
  void Register(std::span<const Definition> definitions,
                DeclarationCategory domain) {
    for (const Definition &definition : definitions) {
      RecipeDeclaration node;
      node.definition = definition;
      node.name = definition.name;
      node.category = domain;
      node.displayName = Match(
          node.definition, [](const Signal &r) { return SignalWhere(r.name); },
          [](const Source &r) { return SourceWhere(r.name); },
          [](const Mask &r) { return MaskWhere(r.name); },
          [](const Curve &r) { return CurveWhere(r.name); });
      if (!IsName(node.name)) {
        Reject(node, "names are letters, digits and underscores, not starting "
                     "with a digit");
      }
      const auto [it, inserted] = graph.nodeIndicesByName_.emplace(
          node.name, graph.declarations_.size());
      if (!inserted) {
        auto &prior = graph.declarations_[it->second];
        Reject(node, std::format("duplicate name; also declared at {}",
                                 prior.displayName));
        Reject(prior, std::format("duplicate name; also declared at {}",
                                  node.displayName));
      }
      graph.declarations_.push_back(std::move(node));
    }
  }

  void Parse(RecipeDeclaration &node, std::string_view text, bool curve) {
    auto parsed = curve ? ParseCurve(text) : Program::Parse(text);
    if (!parsed) {
      Reject(node, parsed.error());
      return;
    }
    node.expression = DeclarationExpression{std::move(*parsed), {}, {}};
    node.mayChangeOverTime =
        node.mayChangeOverTime || node.expression->program.UsesTime();
    if (!curve && node.expression->program.UsesX()) {
      Reject(node, "'x' is only defined inside a curve");
    }
    if (!curve && node.expression->program.UsesMean())
      Reject(node, "'mean' is only defined inside a curve");
  }

  void AddFunction(std::string owner, const CurveRef &reference,
                   std::optional<std::size_t> consumer) {
    std::optional<std::size_t> function;
    if (const auto name = reference.Named()) {
      function = graph.FindNodeIndex(*name);
      if (!function || graph.declarations_[*function].category !=
                           DeclarationCategory::kFunction) {
        Reporter{graph.diagnostics_, owner}.Error(
            std::format("curve names unknown curve '@{}'", *name));
        if (consumer)
          graph.declarations_[*consumer].isDisabled = true;
        return;
      }
    } else {
      function = graph.Size();
      RecipeDeclaration node;
      node.definition = Curve{"", reference.text, ""};
      node.name = std::format("$function{}", *function);
      node.displayName = owner;
      node.category = DeclarationCategory::kFunction;
      graph.declarations_.push_back(std::move(node));
    }
    graph.resultTransformsByLocation_.emplace(std::move(owner), *function);
    if (consumer) {
      auto &node = graph.declarations_[*consumer];
      node.resultTransform = *function;
      node.dependencies.push_back(*function);
    }
  }

  void AppliedFunctions(const Recipe &recipe) {
    for (std::size_t i = 0; i < recipe.signals.size(); ++i) {
      const auto &signal = recipe.signals[i];
      if (signal.curve)
        AddFunction(SignalWhere(signal.name), *signal.curve, i);
    }
    for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
      const auto *surface = Get<SurfaceOutput>(recipe.outputs[i]);
      if (!surface)
        continue;
      for (std::size_t j = 0; j < surface->stack.size(); ++j) {
        if (const auto &curve = surface->stack[j].curve)
          AddFunction(LayerWhere(i, j), *curve, std::nullopt);
      }
    }
  }

  void ParseDefinitions() {
    for (auto &node : graph.declarations_) {
      Match(
          node.definition,
          [&](const Signal &s) {
            node.mayChangeOverTime = SignalAnimated(s.kind);
            if (const auto *expr = Get<ExprSignal>(s.kind))
              Parse(node, expr->text, false);
          },
          [&](const Source &s) {
            node.valueType = SourceType(s);
            node.mayChangeOverTime = Is<RippleSource>(s.kind);
          },
          [&](const Mask &m) { Parse(node, m.text, false); },
          [&](const Curve &c) { Parse(node, c.text, true); });
    }
  }

  std::optional<std::size_t> Bind(RecipeDeclaration &node,
                                  std::string_view name,
                                  DeclarationCategory domain,
                                  bool fieldReadsTick = false) {
    const auto index = graph.FindNodeIndex(name);
    if (!index) {
      Reject(node,
             std::format("reads unknown {} '@{}'",
                         domain == DeclarationCategory::kFunction ? "curve"
                         : domain == DeclarationCategory::kSignal ? "signal"
                                                                  : "row",
                         name));
      return std::nullopt;
    }
    const auto &dependency = graph.declarations_[*index];
    if (dependency.category != domain &&
        (!fieldReadsTick ||
         dependency.category != DeclarationCategory::kSignal)) {
      Reject(node, std::format("'@{}' has the wrong evaluation domain", name));
      return std::nullopt;
    }
    if (!std::ranges::contains(node.dependencies, *index))
      node.dependencies.push_back(*index);
    return index;
  }

  void BindSignals(RecipeDeclaration &node,
                   std::span<const std::string> names) {
    for (const std::string &name : names)
      (void)Bind(node, name, DeclarationCategory::kSignal);
  }

  void BindExpression(RecipeDeclaration &node) {
    if (!node.expression)
      return;
    DeclarationExpression &expression = *node.expression;
    for (const std::string &name : expression.program.References()) {
      const auto index = Bind(node, name, node.category,
                              node.category == DeclarationCategory::kSpatial);
      expression.valueBindings.push_back(
          index.value_or(std::numeric_limits<std::size_t>::max()));
    }
    for (const std::string &name : expression.program.Curves()) {
      const auto index = Bind(node, name, DeclarationCategory::kFunction);
      expression.functionBindings.push_back(
          index.value_or(std::numeric_limits<std::size_t>::max()));
    }
  }

  void BindDependencies() {
    for (RecipeDeclaration &node : graph.declarations_) {
      if (const auto *signal = Get<Signal>(node.definition)) {
        BindSignals(node, Dependencies(*signal, node.expression
                                                    ? &node.expression->program
                                                    : nullptr));
      }
      if (const auto *source = Get<Source>(node.definition))
        BindSignals(node, Dependencies(*source));
      BindExpression(node);
    }
  }

  enum class OrderMark : std::uint8_t { kNone, kOpen, kDone };

  struct OrderFrame {
    std::size_t node;
    std::size_t next = 0;
  };

  void RejectCycle(std::span<const OrderFrame> path, std::size_t dependency) {
    const auto begin = std::ranges::find(path, dependency, &OrderFrame::node);
    const std::ranges::subrange cyclePath{begin, path.end()};
    std::string cycle;
    for (const OrderFrame &frame : cyclePath)
      cycle += graph.declarations_[frame.node].name + " -> ";
    cycle += graph.declarations_[dependency].name;
    for (const OrderFrame &frame : cyclePath)
      Reject(graph.declarations_[frame.node], "cycle: " + cycle);
  }

  void AppendToOrders(std::size_t node) {
    graph.dependencyOrder_.push_back(node);
    if (graph.declarations_[node].category == DeclarationCategory::kSignal)
      graph.signalEvaluationOrder_.push_back(node);
  }

  void Order() {
    std::vector<OrderMark> marks(graph.Size(), OrderMark::kNone);
    std::vector<OrderFrame> path;
    for (std::size_t start = 0; start < graph.Size(); ++start) {
      if (marks[start] != OrderMark::kNone)
        continue;
      marks[start] = OrderMark::kOpen;
      path.push_back({start});
      while (!path.empty()) {
        OrderFrame &frame = path.back();
        RecipeDeclaration &node = graph.declarations_[frame.node];
        if (frame.next == node.dependencies.size()) {
          marks[frame.node] = OrderMark::kDone;
          AppendToOrders(frame.node);
          path.pop_back();
          continue;
        }
        const std::size_t dependency = node.dependencies[frame.next++];
        if (marks[dependency] == OrderMark::kDone)
          continue;
        if (marks[dependency] == OrderMark::kOpen) {
          RejectCycle(path, dependency);
          continue;
        }
        if (path.size() >= kMaxRecipeDepth) {
          Reject(
              node,
              std::format(
                  "dependency chain is deeper than {} rows; this row is inert",
                  kMaxRecipeDepth));
          continue;
        }
        marks[dependency] = OrderMark::kOpen;
        path.push_back({dependency});
      }
    }
  }

  struct ReferenceTypeChecker {
    RecipeGraph &graph;
    RecipeDeclaration &node;

    void Reject(std::string message) const {
      node.isDisabled = true;
      ReportSignal(graph, node.name, std::move(message));
    }

    [[nodiscard]] std::optional<ValueType>
    MismatchedType(const Ref &reference, ValueType expected) const {
      const auto index = graph.FindSignalIndex(reference.name);
      const auto type = index ? graph.TypeOf(*index) : std::nullopt;
      return type && *type != expected ? type : std::nullopt;
    }

    void CheckScalarReference(const Ref &reference,
                              std::string_view field) const {
      if (const auto type = MismatchedType(reference, ValueType::kScalar)) {
        Reject(std::format("'{}' must be a scalar; '@{}' is a {}", field,
                           reference.name, Name(*type)));
      }
    }

    void CheckScalar(const Param &parameter, std::string_view field) const {
      if (const auto *reference = Get<Ref>(parameter)) {
        CheckScalarReference(*reference, field);
      }
    }

    void CheckTrigger(std::string_view reference, std::string message) const {
      const auto index = graph.FindSignalIndex(reference);
      if (index && !Is<TriggerSignal>(graph.SignalAt(*index)->kind)) {
        Reject(std::move(message));
      }
    }

    void RequireReference(std::string_view reference,
                          std::string message) const {
      if (reference.empty()) {
        Reject(std::move(message));
      }
    }

    void CheckColor(const Vec3Param &color) const {
      if (const auto *reference = Get<Ref>(color)) {
        if (const auto type = MismatchedType(*reference, ValueType::kVec3)) {
          Reject(std::format("a stop colour must be a vec3; '@{}' is a {}",
                             reference->name, Name(*type)));
        }
      }
    }

    void operator()(const WaveSignal &k) const {
      CheckScalar(k.base, "base");
      CheckScalar(k.amplitude, "amplitude");
      CheckScalar(k.period, "period");
      CheckScalar(k.phase, "phase");
    }

    void operator()(const RampSignal &k) const {
      CheckScalar(k.from, "from");
      CheckScalar(k.to, "to");
      CheckScalar(k.seconds, "seconds");
    }

    void operator()(const TriggerSignal &k) const {
      CheckScalar(k.lifetime, "lifetime");
      if (Get<WorldAnchor>(k.anchor) && k.payload != ValueType::kVec3) {
        Reject(std::format(
            "an anchor in world space needs a vec3 payload; this trigger "
            "carries a {}",
            Name(k.payload)));
      }
      if (const auto *when = Get<WhenOrigin>(k.origin)) {
        RequireReference(when->when.name, "a when trigger names a signal");
        CheckScalarReference(when->when, "when");
        if (when->value) {
          if (const auto type = MismatchedType(*when->value, k.payload)) {
            Reject(std::format(
                "the trigger's payload is a {}; 'value' reads '@{}', a {}",
                Name(k.payload), when->value->name, Name(*type)));
          }
        }
      }
    }

    void operator()(const ActorValueSignal &k) const {
      RequireReference(k.actorValue,
                       "an actor-value signal names an actor value");
    }

    void operator()(const RateSignal &k) const {
      RequireReference(k.of.name, "a delta signal reads a signal");
    }

    void operator()(const PayloadSignal &k) const {
      RequireReference(k.trigger.name, "a payload signal names a trigger");
      CheckTrigger(
          k.trigger.name,
          std::format("'trigger' must name a trigger; '@{}' is not one",
                      k.trigger.name));
    }

    void operator()(const CounterSignal &k) const {
      RequireReference(k.trigger.name, "a counter signal names a trigger");
      CheckTrigger(k.trigger.name,
                   std::format("'@{}' must be a trigger", k.trigger.name));
      if (k.reset)
        CheckTrigger(k.reset->name,
                     std::format("'@{}' must be a trigger", k.reset->name));
      if (k.cap)
        CheckScalar(*k.cap, "cap");
    }

    void operator()(const AccumulateSignal &k) const {
      RequireReference(k.trigger.name, "an accumulate signal names a trigger");
      CheckTrigger(k.trigger.name,
                   std::format("'@{}' must be a trigger", k.trigger.name));
      CheckScalar(k.decay, "decay");
    }

    void operator()(const NoiseSignal &k) const {
      CheckScalar(k.frequency, "frequency");
      CheckScalar(k.amplitude, "amplitude");
    }

    void operator()(const GradientSignal &k) const {
      CheckScalar(k.t, "t");
      if (k.stops.empty()) {
        Reject("gradient needs at least one stop");
      }
      for (const auto &stop : k.stops) {
        CheckColor(stop.color);
      }
    }

    void operator()(const SmoothSignal &k) const {
      RequireReference(k.of.name, "a smooth signal reads a signal");
      CheckScalar(k.seconds, "seconds");
    }
    template <class T> void operator()(const T &) const {}
  };

  static std::optional<ValueType> NamedSignalType(const RecipeGraph &graph,
                                                  std::string_view name) {
    const auto index = graph.FindSignalIndex(name);
    return index ? graph.TypeOf(*index) : std::nullopt;
  }

  static ValueType TypeOf(EfshField field) {
    switch (field) {
    case EfshField::kFillColor:
    case EfshField::kEdgeColor:
      return ValueType::kVec3;
    case EfshField::kScroll:
      return ValueType::kVec2;
    default:
      return ValueType::kScalar;
    }
  }

  static ValueType PayloadTypeOf(const RecipeGraph &graph,
                                 const PayloadSignal &signal) {
    const auto index = graph.FindSignalIndex(signal.trigger.name);
    const Signal *triggerSignal = index ? graph.SignalAt(*index) : nullptr;
    const auto *trigger =
        triggerSignal ? Get<TriggerSignal>(triggerSignal->kind) : nullptr;
    return trigger ? trigger->payload : ValueType::kScalar;
  }

  static ValueType InferExpressionSignal(RecipeGraph &graph,
                                         RecipeDeclaration &node) {
    if (!node.expression) {
      return ValueType::kScalar;
    }
    auto checked =
        node.expression->program.Check([&graph](std::string_view name) {
          return NamedSignalType(graph, name);
        });
    if (!checked) {
      node.isDisabled = true;
      ReportSignal(graph, node.name, std::format("expr: {}", checked.error()));
      return ValueType::kScalar;
    }
    return *checked;
  }

  static ValueType SignalTypeOf(RecipeGraph &graph, RecipeDeclaration &node) {
    const Signal *signal = Get<Signal>(node.definition);
    if (!signal)
      return ValueType::kScalar;
    return Match(
        signal->kind,
        [](const ConstantSignal &k) {
          return BetterEnchantmentEffects::TypeOf(k.value);
        },
        [](const EfshSignal &k) { return TypeOf(k.field); },
        [&](const PayloadSignal &k) { return PayloadTypeOf(graph, k); },
        [](const GradientSignal &) { return ValueType::kVec3; },
        [&](const RateSignal &k) {
          return NamedSignalType(graph, k.of.name).value_or(ValueType::kScalar);
        },
        [](const ToRootSignal &) { return ValueType::kVec3; },
        [&](const SmoothSignal &k) {
          return NamedSignalType(graph, k.of.name).value_or(ValueType::kScalar);
        },
        [&](const ExprSignal &) { return InferExpressionSignal(graph, node); },
        [](const WaveSignal &) { return ValueType::kScalar; },
        [](const RampSignal &) { return ValueType::kScalar; },
        [](const ActorValueSignal &) { return ValueType::kScalar; },
        [](const ActorStateSignal &k) {
          return VectorValued(k.kind) ? ValueType::kVec3 : ValueType::kScalar;
        },
        [](const EnchantmentSignal &) { return ValueType::kScalar; },
        [](const TriggerSignal &) { return ValueType::kScalar; },
        [](const CounterSignal &) { return ValueType::kScalar; },
        [](const AccumulateSignal &) { return ValueType::kScalar; },
        [](const NoiseSignal &) { return ValueType::kScalar; });
  }

  static void CheckAppliedCurve(RecipeGraph &graph, RecipeDeclaration &node) {
    if (node.resultTransform && node.valueType != ValueType::kScalar) {
      node.isDisabled = true;
      ReportSignal(
          graph, node.name,
          std::format(
              "a curve applies only to a scalar signal; this one is a {}",
              Name(node.valueType)));
    }
  }

  static void InferSignal(RecipeGraph &a_graph, RecipeDeclaration &n) {
    const Signal *signal = Get<Signal>(n.definition);
    if (!signal)
      return;
    n.valueType = SignalTypeOf(a_graph, n);
    CheckAppliedCurve(a_graph, n);
    Match(signal->kind, ReferenceTypeChecker{a_graph, n});
  }

  void AnalyzeCurve(RecipeDeclaration &node) {
    if (node.category != DeclarationCategory::kFunction || !node.expression)
      return;
    const auto checked = node.expression->program.Check(
        [](std::string_view) -> std::optional<ValueType> {
          return std::nullopt;
        });
    if (!checked)
      Reject(node, checked.error());
    else if (*checked != ValueType::kScalar)
      Reject(node, "a curve function must return a scalar");
    if (node.expression->program.UsesTime())
      Reject(node, "'time' is not bound inside a curve function");
  }

  void ReportOnce(const Diagnostic &diagnostic) {
    const bool reported =
        std::ranges::any_of(graph.diagnostics_, [&](const Diagnostic &prior) {
          return prior.where == diagnostic.where &&
                 prior.message == diagnostic.message &&
                 prior.severity == diagnostic.severity;
        });
    if (!reported)
      graph.diagnostics_.push_back(diagnostic);
  }

  void AnalyzeSource(RecipeDeclaration &node, const Source &source,
                     const Recipe &recipe) {
    for (const Diagnostic &diagnostic :
         CheckSourceInputs(RowTypes{recipe, graph}, source)) {
      if (diagnostic.severity == Severity::kError)
        node.isDisabled = true;
      ReportOnce(diagnostic);
    }
  }

  void AnalyzeSpatial(RecipeDeclaration &node) {
    if (node.category != DeclarationCategory::kSpatial || !node.expression ||
        node.isDisabled)
      return;
    const auto checked = node.expression->program.Check(
        [&](std::string_view name) { return graph.TypeOf(name); });
    if (checked)
      node.valueType = *checked;
    else
      Reject(node, checked.error());
  }

  void InheritDependencyState(RecipeDeclaration &node) {
    for (const std::size_t dependency : node.dependencies) {
      const RecipeDeclaration &input = graph.declarations_[dependency];
      node.mayChangeOverTime =
          node.mayChangeOverTime || input.mayChangeOverTime;
      if (input.isDisabled && !node.isDisabled) {
        node.isDisabled = true;
        Reporter{graph.diagnostics_, node.displayName}.Warn(
            std::format("inert because '@{}' is", input.name));
      }
    }
  }

  void Analyze(const Recipe &recipe) {
    for (const std::size_t index : graph.dependencyOrder_) {
      RecipeDeclaration &node = graph.declarations_[index];
      InferSignal(graph, node);
      AnalyzeCurve(node);
      if (const auto *source = Get<Source>(node.definition))
        AnalyzeSource(node, *source, recipe);
      AnalyzeSpatial(node);
      InheritDependencyState(node);
    }
  }

  static bool RowsFit(const Recipe &recipe) {
    return recipe.signals.size() <= kMaxRecipeRows &&
           recipe.sources.size() <= kMaxRecipeRows &&
           recipe.masks.size() <= kMaxRecipeRows &&
           recipe.curves.size() <= kMaxRecipeRows &&
           recipe.outputs.size() <= kMaxRecipeRows &&
           std::ranges::none_of(recipe.outputs, [](const Output &output) {
             const auto *surface = Get<SurfaceOutput>(output);
             return surface && surface->stack.size() > kMaxRecipeRows;
           });
  }

  static bool OutputBindingsFit(const Recipe &recipe) {
    std::size_t bindingBudget = 7;
    for (const Output &output : recipe.outputs) {
      const auto *surface = Get<SurfaceOutput>(output);
      bindingBudget += surface ? 12 + 4 * surface->stack.size() : 5;
      if (bindingBudget > 16 * kMaxRecipeRows)
        return false;
    }
    return true;
  }

  static bool CompiledRowsFit(const Recipe &recipe) {
    std::size_t compiledNodes = recipe.signals.size() + recipe.sources.size() +
                                recipe.masks.size() + recipe.curves.size();
    const auto countFunction = [&](const std::optional<CurveRef> &function) {
      if (function && !function->Named())
        ++compiledNodes;
      return compiledNodes <= 4 * kMaxRecipeRows;
    };
    const bool signalsFit =
        std::ranges::all_of(recipe.signals, [&](const Signal &signal) {
          return countFunction(signal.curve);
        });
    return signalsFit &&
           std::ranges::all_of(recipe.outputs, [&](const Output &output) {
             const auto *surface = Get<SurfaceOutput>(output);
             return !surface || std::ranges::all_of(
                                    surface->stack, [&](const Layer &layer) {
                                      return countFunction(layer.curve);
                                    });
           });
  }

  static std::optional<std::string>
  CompilationLimitProblem(const Recipe &recipe) {
    if (!RowsFit(recipe))
      return "row count exceeds compilation limit";
    if (!OutputBindingsFit(recipe))
      return "output bindings exceed compilation budget";
    if (!CompiledRowsFit(recipe))
      return "compiled rows and inline functions exceed the graph budget";
    return std::nullopt;
  }
};

RecipeGraph RecipeGraph::Compile(const Recipe &recipe) {
  RecipeGraphBuilder builder;
  if (const std::optional<std::string> problem =
          RecipeGraphBuilder::CompilationLimitProblem(recipe)) {
    Reporter{builder.graph.diagnostics_, "recipe"}.Error(*problem);
    return std::move(builder.graph);
  }
  builder.Register(std::span{recipe.signals}, DeclarationCategory::kSignal);
  builder.Register(std::span{recipe.sources}, DeclarationCategory::kSpatial);
  builder.Register(std::span{recipe.masks}, DeclarationCategory::kSpatial);
  builder.Register(std::span{recipe.curves}, DeclarationCategory::kFunction);
  builder.AppliedFunctions(recipe);
  builder.ParseDefinitions();
  builder.BindDependencies();
  builder.Order();
  builder.Analyze(recipe);
  LowerRecipeGraph(builder.graph, recipe);
  return std::move(builder.graph);
}

}
