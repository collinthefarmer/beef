// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/RecipeGraph.h"
#include "recipe/Signals.h"
#include "test_support.h"

#include <algorithm>
#include <limits>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
const RecipeNode *Node(const RecipeGraph &graph, std::string_view name) {
  const auto index = graph.FindNodeIndex(name);
  return index ? graph.NodeAt(*index) : nullptr;
}
bool IsDisabled(const RecipeGraph &graph, std::string_view name) {
  const auto index = graph.FindNodeIndex(name);
  return index && graph.IsDisabled(*index);
}
bool Message(const RecipeGraph &graph, std::string_view where,
             std::string_view text) {
  return std::ranges::any_of(
      graph.Diagnostics(), [&](const Diagnostic &diagnostic) {
        return diagnostic.where == where && diagnostic.message.contains(text);
      });
}
}

int main() {
  {
    Recipe recipe;
    recipe.signals = {{"placement", ConstantSignal{Vec2{1, 2}}},
                      {"clock", ExprSignal{"time"}}};
    ImageSource image;
    image.path = "test.dds";
    image.channel = ImageChannel::kR;
    image.tile = Ref{"placement"};
    recipe.sources = {{"image", image}};
    recipe.curves = {{"shape", "x * x"}};
    recipe.masks = {{"outer", "@shape(@inner)"},
                    {"inner", "@image * 0.5"},
                    {"moving", "@outer * @clock"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(graph.Diagnostics().empty(), "mixed rows compile together");
    Check(!graph.MayChangeOverTime("outer") &&
              graph.MayChangeOverTime("moving"),
          "constant placement remains static and time propagates to consumers");
    Check(graph.TypeOf("outer") == ValueType::kScalar,
          "mask types follow dependencies in topological order");
    Check(graph.SignalEvaluationOrder().size() == 2 &&
              graph.Functions().size() == 1,
          "runtime signal order excludes fields and functions");
    for (const auto index : graph.DependencyOrder()) {
      const auto *node = graph.NodeAt(index);
      Check(node != nullptr, "order contains valid handles");
      if (!node)
        continue;
      const auto position = std::ranges::find(graph.DependencyOrder(), index);
      for (const auto dependency : InputsOf(node->kind)) {
        Check(std::ranges::find(graph.DependencyOrder(), dependency.node) <
                  position,
              "every valid dependency precedes its consumer");
      }
    }
    Check(graph.SampleDependent({*graph.FindNodeIndex("image"), 0}) &&
              !graph.SampleDependent(*graph.FindSignalOutput("clock")),
          "sample dependence follows explicit UV input connections");
    const auto *clock = graph.ExpressionAt(*graph.FindNodeIndex("clock"));
    Check(clock && !clock->program.UsesTime() &&
              clock->valueBindings.size() == 1,
          "expression time is bound as an ordinary graph input");
  }
  {
    Recipe recipe;
    recipe.masks = {
        {"a", "@b"}, {"b", "@a"}, {"consumer", "@a"}, {"okay", "1"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(IsDisabled(graph, "a") && IsDisabled(graph, "b") &&
              IsDisabled(graph, "consumer") && !IsDisabled(graph, "okay"),
          "cycles disable only participants and dependents");
    Check(Message(graph, "mask a", "a -> b -> a") &&
              Message(graph, "mask b", "cycle"),
          "cycle diagnostics name both rows and the path");
  }
  {
    Recipe recipe;
    recipe.signals = {{"same", ConstantSignal{1.0f}}};
    recipe.sources = {{"same", MaterialSource{}}};
    recipe.curves = {{"same", "x"}};
    recipe.masks = {{"consumer", "@same"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(Message(graph, "signal same", "duplicate") &&
              Message(graph, "source same", "duplicate") &&
              Message(graph, "curve same", "duplicate"),
          "all duplicate declarations are diagnosed");
    Check(IsDisabled(graph, "consumer"),
          "ambiguous references cannot select a shadowed row");
    SignalState state{graph};
    Check(test::Near(state.Scalar("same"), 0.0f),
          "duplicate constants do not initialize live values");
  }
  {
    Recipe recipe;
    recipe.signals = {{"bad", ExprSignal{"@field"}},
                      {"okay", ConstantSignal{2.0f}}};
    recipe.masks = {
        {"field", "1"}, {"missing", "@absent"}, {"dependent", "@missing"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(IsDisabled(graph, "bad") && IsDisabled(graph, "missing") &&
              IsDisabled(graph, "dependent"),
          "domain violations and unknown references propagate inertness");
    Check(!IsDisabled(graph, "okay") && !graph.FindSignalIndex("field"),
          "unrelated signals remain usable");
    Check(graph.NodeAt(std::numeric_limits<std::size_t>::max()) == nullptr &&
              graph.IsDisabled(graph.Size()) && !graph.TypeOf(graph.Size()),
          "invalid handles fail safely");
  }
  {
    Recipe recipe;
    recipe.signals = {{"named", ConstantSignal{0.5f}, CurveRef{"@shape"}},
                      {"inline", ConstantSignal{0.5f}, CurveRef{"x * x"}}};
    recipe.curves = {{"shape", "x * x"}};
    SurfaceOutput output;
    Layer layer;
    layer.curve = CurveRef{"x * x"};
    output.stack.push_back(layer);
    recipe.outputs.push_back(output);
    const auto graph = RecipeGraph::Compile(recipe);
    const auto *named = Get<CallOperation>(Node(graph, "named")->kind);
    const auto *inlined = Get<CallOperation>(Node(graph, "inline")->kind);
    Check(named && inlined && named->function != inlined->function &&
              graph.FindFunction("shape") == named->function,
          "named and inline functions both lower to ordinary calls");
    Check(graph.TransformFor(SignalWhere("named")) == named->function &&
              graph.TransformFor(LayerWhere(0, 0)),
          "applied functions bind to scoped function definitions");
    const auto *function = graph.FunctionAt(named->function);
    Check(function && function->parameters.size() == 2 &&
              Is<ParameterOperation>(function->nodes.front().kind),
          "function arguments have local parameter nodes");
    SignalState state{graph};
    state.Tick(NullEnvironment{}, {});
    Check(test::Near(state.Scalar("named"), 0.25f) &&
              test::Near(state.Scalar("inline"), 0.25f),
          "named and inline function execution agrees");
  }
  {
    Recipe recipe;
    recipe.signals = {{"scalar", ConstantSignal{1.0f}}};
    ImageSource image;
    image.path = "test.dds";
    image.tile = Ref{"scalar"};
    recipe.sources = {{"badImage", image}};
    recipe.masks = {{"dependent", "@badImage"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(IsDisabled(graph, "badImage") && IsDisabled(graph, "dependent"),
          "source parameter type failures propagate to masks");
  }
  {
    Recipe recipe;
    recipe.masks = {
        {"broken", "@"}, {"dependent", "@broken"}, {"self", "@self"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(IsDisabled(graph, "broken") && IsDisabled(graph, "dependent") &&
              IsDisabled(graph, "self"),
          "malformed programs and self references are inert");
    recipe.masks.resize(kMaxRecipeRows + 1);
    const auto oversized = RecipeGraph::Compile(recipe);
    Check(oversized.Size() == 0 && !oversized.Diagnostics().empty(),
          "typed models obey compilation bounds");
  }
  {
    Recipe recipe;
    recipe.curves = {{"vector", "[x, x]"}, {"clock", "time"}};
    recipe.signals = {{"consumer", ConstantSignal{1.0f}, CurveRef{"@vector"}}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(graph.FunctionAt(*graph.FindFunction("vector"))->isDisabled &&
              graph.FunctionAt(*graph.FindFunction("clock"))->isDisabled &&
              IsDisabled(graph, "consumer"),
          "functions enforce scalar result and bound-variable contracts");
  }
  {
    Recipe original;
    original.signals = {{"level", ConstantSignal{0.25f}}};
    original.masks = {{"mask", "@level"}};
    SurfaceOutput output;
    Layer layer;
    layer.source = Ref{"mask"};
    output.stack.push_back(layer);
    const auto base = RecipeGraph::Compile(original);
    Check(ShareableAcrossActors(original, base, original.masks.front()) &&
              ShareableAcrossActors(original, base, Output{output}),
          "unchanged static signals can share rendered results");
    Recipe variant = original;
    variant.signals.front().kind = ConstantSignal{0.75f};
    const auto overridden = RecipeGraph::Compile(variant);
    Check(
        !ShareableAcrossActors(original, overridden, original.masks.front()) &&
            !ShareableAcrossActors(original, overridden, Output{output}),
        "variant overrides cannot reuse targets keyed by the original recipe");
  }
  {
    Recipe recipe;
    recipe.signals.resize(kMaxRecipeRows);
    recipe.sources.resize(kMaxRecipeRows);
    recipe.masks.resize(kMaxRecipeRows);
    recipe.curves.resize(kMaxRecipeRows);
    recipe.signals.front().curve = CurveRef{"x"};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(graph.Size() == 0 && Message(graph, "recipe", "graph budget"),
          "anonymous functions count toward the total compilation budget");
  }
  {
    Recipe recipe;
    ImageSource image;
    image.path = "test.dds";
    image.scroll = std::array<Param, 2>{0.25f, 0.5f};
    recipe.sources = {{"offset", image}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(!graph.MayChangeOverTime("offset"),
          "constant image offsets do not introduce a time dependency");
    const auto *normalized =
        Get<ExpressionOperation>(Node(graph, "offset")->kind);
    const auto *sampleNode =
        normalized && !normalized->expression.valueBindings.empty()
            ? graph.NodeAt(normalized->expression.valueBindings.front().node)
            : nullptr;
    const auto *sample =
        sampleNode ? Get<ImageOperation>(sampleNode->kind) : nullptr;
    const auto *coordinates =
        sample ? graph.NodeAt(sample->coordinates.node) : nullptr;
    Check(sample &&
              graph.OutputType(sample->texture) ==
                  GraphValueType{ResourceType::kTexture} &&
              coordinates && Is<TextureCoordinatesOperation>(coordinates->kind),
          "image sampling consumes explicit texture and coordinate inputs");
  }
  {
    Recipe recipe;
    recipe.signals = {{"drift", ExprSignal{"time * 0.1"}}};
    ImageSource image;
    image.path = "test.dds";
    image.scroll = std::array<Param, 2>{Ref{"drift"}, 0.0f};
    image.tile = std::array<Param, 2>{2.0f, 2.0f};
    recipe.sources = {{"drifting", image}};
    const auto graph = RecipeGraph::Compile(recipe);
    const auto *normalized =
        Get<ExpressionOperation>(Node(graph, "drifting")->kind);
    const auto bindings = normalized ? normalized->expression.valueBindings
                                     : std::vector<OutputRef>{};
    const auto *sampleNode =
        bindings.size() == 2 ? graph.NodeAt(bindings[0].node) : nullptr;
    const auto *meanNode =
        bindings.size() == 2 ? graph.NodeAt(bindings[1].node) : nullptr;
    const auto *mean =
        meanNode ? Get<ReductionOperation>(meanNode->kind) : nullptr;
    const auto *lumaNode = mean ? graph.NodeAt(mean->value.node) : nullptr;
    const auto *luma =
        lumaNode ? Get<ExpressionOperation>(lumaNode->kind) : nullptr;
    const auto *measuredNode =
        luma && !luma->expression.valueBindings.empty()
            ? graph.NodeAt(luma->expression.valueBindings.front().node)
            : nullptr;
    const auto *sampled =
        sampleNode ? Get<ImageOperation>(sampleNode->kind) : nullptr;
    const auto *measured =
        measuredNode ? Get<ImageOperation>(measuredNode->kind) : nullptr;
    const auto CoordinatesOf =
        [&](const ImageOperation *op) -> const TextureCoordinatesOperation * {
      const auto *node = op ? graph.NodeAt(op->coordinates.node) : nullptr;
      return node ? Get<TextureCoordinatesOperation>(node->kind) : nullptr;
    };
    const auto *shown = CoordinatesOf(sampled);
    const auto *covered = CoordinatesOf(measured);
    Check(graph.MayChangeOverTime("drifting") && shown && shown->scroll &&
              covered && !covered->scroll && covered->tile &&
              measured->texture == sampled->texture,
          "the image mean measures the unscrolled coverage of the same "
          "texture, so a moving scroll does not refresh it");
  }
  {
    Recipe recipe;
    recipe.signals = {{"drift", ExprSignal{"time * 0.1"}}};
    ImageSource image;
    image.path = "test.dds";
    image.channel = ImageChannel::kLuma;
    image.scroll = std::array<Param, 2>{Ref{"drift"}, 0.0f};
    recipe.sources = {{"gloss", image}};
    recipe.curves = {{"punchy", "(x - mean) * 2 + 0.5"}};
    SurfaceOutput output;
    Layer layer;
    layer.source = Ref{"gloss"};
    layer.curve = CurveRef{"@punchy"};
    output.stack.push_back(layer);
    recipe.outputs.push_back(output);
    const auto graph = RecipeGraph::Compile(recipe);
    std::size_t reductions = 0;
    bool unscrolled = true;
    for (std::size_t i = 0; i < graph.Size(); ++i) {
      const auto *reduction = Get<ReductionOperation>(graph.NodeAt(i)->kind);
      if (!reduction)
        continue;
      ++reductions;
      const auto *measured = graph.NodeAt(reduction->value.node);
      const auto *sample =
          measured ? Get<ImageOperation>(measured->kind) : nullptr;
      const auto *coordinates =
          sample ? graph.NodeAt(sample->coordinates.node) : nullptr;
      const auto *uv = coordinates
                           ? Get<TextureCoordinatesOperation>(coordinates->kind)
                           : nullptr;
      unscrolled = unscrolled && uv && !uv->scroll;
    }
    Check(graph.Diagnostics().empty() && reductions == 1 && unscrolled,
          "a curve mean over a scrolled image measures its unscrolled "
          "coverage");
  }
  {
    Recipe recipe;
    recipe.signals = {{"unbound", ExprSignal{"x"}}};
    recipe.masks = {{"unboundMean", "mean"}};
    SurfaceOutput output;
    Layer layer;
    layer.source = Vec3{1, 1, 1};
    layer.curve = CurveRef{"time"};
    output.stack.push_back(layer);
    recipe.outputs.push_back(output);
    const auto graph = RecipeGraph::Compile(recipe);
    Check(IsDisabled(graph, "unbound") && IsDisabled(graph, "unboundMean"),
          "function parameters cannot leak into ordinary expressions");
    const auto binding = std::ranges::find_if(
        graph.OutputBindings(), [](const OutputBinding &b) {
          return b.property == "output 0 layer 0 source";
        });
    Check(binding != graph.OutputBindings().end() &&
              graph.IsDisabled(binding->value.node),
          "an invalid function disables its mapped output consumer");
  }
  {
    Recipe recipe;
    SurfaceOutput output;
    output.stack.resize(kMaxRecipeRows);
    recipe.outputs.assign(5, output);
    const auto graph = RecipeGraph::Compile(recipe);
    Check(
        graph.Size() == 0 && Message(graph, "recipe", "output bindings"),
        "aggregate layer bindings are bounded before lowering allocates nodes");
  }
  return test::Finish("recipegraph");
}
