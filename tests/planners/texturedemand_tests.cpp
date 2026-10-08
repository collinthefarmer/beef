// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Mesh.h"
#include "planners/TextureDemand.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;
namespace {
ValueBindings Bindings(std::string instance = "instance",
                       std::string material = "material") {
  return {[instance, material](const ExternalSource &source)
              -> std::expected<std::string, std::string> {
            if (Is<MaterialInput>(source))
              return material;
            if (const auto *texture = Get<TextureInput>(source))
              return texture->path;
            if (const auto *position = Get<NodePositionInput>(source))
              return position->name;
            return instance;
          },
          [instance](NodeId node) {
            return instance + ":" + std::to_string(node);
          }};
}
TextureValue TextureValueOf(const RecipeGraph &graph, std::string_view name,
                            std::size_t instance = 0) {
  return {
      &graph, {graph.FindNodeIndex(name).value_or(graph.Size()), 0}, instance};
}
auto Identity(const RecipeGraph &graph, std::string_view name,
              ValueBindings bindings = Bindings()) {
  return IdentifyValue(graph, TextureValueOf(graph, name).output, bindings);
}
}
int main() {
  Check(PositionBakeIdentity(ValueIdentity{"mesh:1"}) !=
            PositionBakeIdentity(ValueIdentity{"mesh:2"}),
        "two meshes give two position bake identities");
  Check(PositionBakeIdentity(ValueIdentity{"a|b"}) !=
            PositionBakeIdentity(ValueIdentity{"a"}),
        "a separator inside a mesh identity cannot forge another identity");
  Recipe first;
  first.id = "first";
  first.signals = {{"gain", ConstantSignal{0.5f}}};
  first.sources = {{"surface", MaterialSource{MaterialChannel::kRoughness}}};
  first.curves = {{"shape", "x * x + mean"}};
  first.masks = {{"mask", "@shape(@surface) * @gain"},
                 {"outer", "@mask + 0.1"}};
  Recipe renamed;
  renamed.id = "renamed";
  renamed.signals = {{"unrelated", ConstantSignal{9.0f}},
                     {"strength", ConstantSignal{0.5f}}};
  renamed.sources = {{"metal", MaterialSource{MaterialChannel::kRoughness}}};
  renamed.curves = {{"response", "x * x + mean"}};
  renamed.masks = {{"result", "@response(@metal) * @strength"}};
  const auto graph = RecipeGraph::Compile(first);
  const auto other = RecipeGraph::Compile(renamed);
  Check(Identity(graph, "mask") == Identity(other, "result"),
        "recipe, node and function renames and unrelated insertions preserve "
        "identity");
  Check(Identity(graph, "mask") !=
            Identity(graph, "mask", Bindings("instance", "other material")),
        "bound resource changes separate otherwise equal computations");
  renamed.curves.front().text = "x + mean";
  const auto changedFunction = RecipeGraph::Compile(renamed);
  Check(Identity(graph, "mask") != Identity(changedFunction, "result"),
        "function body participates in identity");
  renamed.curves.front().text = "x * x + mean";
  renamed.signals.back().kind = ConstantSignal{0.7f};
  const auto changedInput = RecipeGraph::Compile(renamed);
  Check(Identity(graph, "mask") != Identity(changedInput, "result"),
        "dependency values participate in identity");
  std::vector<TextureDemand> demands;
  const TextureRequirements small{SideRef{0}};
  const TextureRequirements large{SideRef{1}};
  const TextureUse use{PlacementId{1}, 0, 0, TextureUseInput::kSource};
  const TextureUse secondUse{PlacementId{2}, 3, 1, TextureUseInput::kMask};
  auto a = CollectTextureDemand(
      demands, {TextureValueOf(graph, "mask"), small, use}, Bindings());
  auto b = CollectTextureDemand(
      demands, {TextureValueOf(other, "result", 1), small, secondUse},
      Bindings());
  Check(a && b && *a == *b && demands.size() == 2,
        "equivalent requests across recipes coalesce with their dependencies");
  if (a) {
    Check(demands[*a].program.has_value() && demands[*a].dependents.size() == 2,
          "compiled demand retains both consumer locations");
    Check(demands[*a].dependencies.size() == 1 &&
              demands[*a].dependencies[0] < *a,
          "dependencies precede acquisition of their consumer");
    Check(demands.front().dependents.size() == 2,
          "shared prerequisite retains every affected consumer for failure "
          "reporting");
  }
  auto bigger = CollectTextureDemand(
      demands, {TextureValueOf(graph, "mask"), large, use}, Bindings());
  Check(a && bigger && *a != *bigger && demands.size() == 4,
        "different sizes never silently share textures or prerequisites");
  auto nested = CollectTextureDemand(
      demands, {TextureValueOf(graph, "outer"), small, use}, Bindings());
  Check(nested && a && demands[*nested].dependencies.front() == *a,
        "nested mask reuses collected intermediate");
  const auto before = demands.size();
  auto invalid = CollectTextureDemand(
      demands, {{&graph, {graph.Size(), 0}, 0}, small, use}, Bindings());
  Check(!invalid && demands.size() == before,
        "invalid handles do not mutate accepted demands");
  auto port = TextureValueOf(graph, "mask");
  port.output.output = 1;
  Check(!CollectTextureDemand(demands, {port, small, use}, Bindings()),
        "invalid output port is rejected");
  Recipe dynamic;
  dynamic.signals = {{"wave", WaveSignal{}}, {"otherWave", WaveSignal{}}};
  dynamic.masks = {{"a", "@wave"}, {"b", "@otherWave"}, {"clock", "time"}};
  const auto stateful = RecipeGraph::Compile(dynamic);
  Check(Identity(stateful, "a") != Identity(stateful, "b"),
        "independent state owners do not coalesce within an instance");
  Check(Identity(stateful, "a") != Identity(stateful, "a", Bindings("another")),
        "independent execution instances retain distinct state");
  Check(Identity(stateful, "clock") !=
            Identity(stateful, "clock", Bindings("another")),
        "clock identity tracks the runtime binding");
  Recipe geometry;
  geometry.sources = {{"position", BakeSource{PositionBake{}}},
                      {"same", BakeSource{PositionBake{}}},
                      {"left", DistanceSource{"left"}},
                      {"right", DistanceSource{"right"}}};
  const auto geometryGraph = RecipeGraph::Compile(geometry);
  Check(Identity(geometryGraph, "position") == Identity(geometryGraph, "same"),
        "equivalent named bakes share computation identity");
  Check(Identity(geometryGraph, "left") != Identity(geometryGraph, "right"),
        "distinct resolved origin bindings remain distinct");
  Check(DistanceBakeIdentity({1, 2, 3}) != DistanceBakeIdentity({1, 2, 4}),
        "distance cache identifies resolved position");
  Recipe item;
  item.sources = {{"item", PieceSource{PieceField::kItemHash}}};
  item.masks = {{"partCount", "1 + floor(@item * 7)"}};
  const auto itemGraph = RecipeGraph::Compile(item);
  const std::optional<std::size_t> itemNode = itemGraph.FindNodeIndex("item");
  const RecipeNode *itemSource =
      itemNode ? itemGraph.NodeAt(*itemNode) : nullptr;
  const auto *itemInput =
      itemSource ? Get<ExternalInput>(itemSource->kind) : nullptr;
  Check(itemInput && Is<ItemHashInput>(itemInput->source),
        "an itemHash bake lowers to the per-geometry item hash input");
  Check(itemNode && !itemGraph.SampleDependent({*itemNode, 0}),
        "the item hash is uniform over the piece");
  Recipe counted;
  counted.sources = {{"charts", PieceSource{PieceField::kChartCount}}};
  const auto countedGraph = RecipeGraph::Compile(counted);
  const std::optional<std::size_t> chartsNode =
      countedGraph.FindNodeIndex("charts");
  const RecipeNode *chartsSource =
      chartsNode ? countedGraph.NodeAt(*chartsNode) : nullptr;
  const auto *chartsInput =
      chartsSource ? Get<ExternalInput>(chartsSource->kind) : nullptr;
  Check(chartsInput && Is<ChartCountInput>(chartsInput->source) &&
            !countedGraph.SampleDependent({*chartsNode, 0}),
        "a chartCount bake lowers to a uniform per-piece input");
  const std::optional<std::size_t> countNode =
      itemGraph.FindNodeIndex("partCount");
  Check(countNode && !std::ranges::contains(itemGraph.SignalEvaluationOrder(),
                                            *countNode),
        "expressions over the item hash wait for the geometry binding");
  const auto ItemBindings = [](std::string a_hash) {
    ValueBindings bindings = Bindings();
    bindings.external = [a_hash](const ExternalSource &source)
        -> std::expected<std::string, std::string> {
      return Is<ItemHashInput>(source) ? a_hash : std::string{"instance"};
    };
    return bindings;
  };
  Check(Identity(itemGraph, "partCount", ItemBindings("item:1")) !=
            Identity(itemGraph, "partCount", ItemBindings("item:2")),
        "pieces with different items keep distinct computations");
  Check(Identity(itemGraph, "partCount", ItemBindings("item:1")) ==
            Identity(itemGraph, "partCount", ItemBindings("item:1")),
        "copies of one item share a computation");
  Recipe bad;
  bad.masks = {{"broken", "@missing"}};
  const auto disabled = RecipeGraph::Compile(bad);
  Check(!CollectTextureDemand(demands,
                              {TextureValueOf(disabled, "broken"), small, use},
                              Bindings()),
        "disabled producers fail before acquisition");
  Recipe excessive;
  std::string expression;
  for (int i = 0; i < 9; ++i) {
    const auto name = "texture" + std::to_string(i);
    ImageSource image;
    image.path = name + ".dds";
    excessive.sources.push_back({name, image});
    if (i)
      expression += "+";
    expression += "@" + name;
  }
  excessive.masks = {{"tooMany", expression}};
  const auto limits = RecipeGraph::Compile(excessive);
  const auto failed = CollectTextureDemand(
      demands, {TextureValueOf(limits, "tooMany"), small, use}, Bindings());
  Check(!failed && demands.size() == before,
        "backend limit failure leaves no partially collected request");
  return test::Finish("texture demand");
}
