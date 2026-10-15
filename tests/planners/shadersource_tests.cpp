// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"
#include "planners/ShaderSource.h"
#include "planners/StackShader.h"
#include "recipe/Importer.h"
#include "recipe/RecipeGraph.h"
#include "render/ShaderConstants.h"
#include "test_support.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct ConstantBuffer {
  std::string_view name;
  std::size_t bytes;
};

constexpr ConstantBuffer kConstantBuffers[]{
    {"Params", sizeof(LayerConstants)},
    {"ProgramParams", sizeof(ProgramConstants)},
    {"ReduceParams", sizeof(ReductionConstants)},
    {"RippleParams", sizeof(RippleConstants)},
    {"StackParams", sizeof(StackConstants)},
    {"GroupParams", sizeof(GroupConstants)},
    {"DiffuseDetailParams", sizeof(DiffuseDetailConstants)},
    {"BakeNoiseParams", sizeof(BakeNoiseConstants)},
    {"FilterParams", sizeof(FilterConstants)},
};

struct EntryPoint {
  std::string_view name;
  std::string_view profile;
};

constexpr EntryPoint kEntryPoints[]{
    {"VSMain", "vs_6_0"},     {"PSMain", "ps_6_0"},
    {"PSProgram", "ps_6_0"},  {"PSRipple", "ps_6_0"},
    {"PSClusters", "ps_6_0"}, {"PSDiffuseDetail", "ps_6_0"},
    {"DilatePS", "ps_6_0"},   {"PSReduce", "ps_6_0"},
    {"PSStack", "ps_6_0"},    {"BakeVS", "vs_6_0"},
    {"BakePS", "ps_6_0"},     {"BakeNoisePS", "ps_6_0"},
    {"PSFilter", "ps_6_0"},
};

struct FieldType {
  std::string_view name;
  std::size_t bytes;
};

constexpr FieldType kFieldTypes[]{
    {"float4", 16}, {"int4", 16}, {"uint4", 16}, {"float4x4", 64}};

std::string_view Trimmed(std::string_view a_text) {
  const auto first = a_text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = a_text.find_last_not_of(" \t\r\n");
  return a_text.substr(first, last - first + 1);
}

std::optional<std::size_t> FieldBytes(std::string_view a_field) {
  const auto space = a_field.find_first_of(" \t");
  if (space == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view type = a_field.substr(0, space);
  const auto *known = std::ranges::find(kFieldTypes, type, &FieldType::name);
  if (known == std::ranges::end(kFieldTypes)) {
    return std::nullopt;
  }
  const auto open = a_field.find('[');
  if (open == std::string_view::npos) {
    return known->bytes;
  }
  const auto close = a_field.find(']', open);
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t count = 0;
  const std::string_view digits = a_field.substr(open + 1, close - open - 1);
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), count);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return known->bytes * count;
}

std::optional<std::size_t> PackedBytes(std::string_view a_source,
                                       std::string_view a_buffer) {
  const std::string heading = std::format("cbuffer {} :", a_buffer);
  const auto at = a_source.find(heading);
  const auto open = at == std::string_view::npos ? at : a_source.find('{', at);
  const auto close =
      open == std::string_view::npos ? open : a_source.find('}', open);
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view body = a_source.substr(open + 1, close - open - 1);
  std::size_t total = 0;
  while (!body.empty()) {
    const auto end = body.find(';');
    const std::string_view field = Trimmed(body.substr(0, end));
    if (!field.empty()) {
      const std::optional<std::size_t> bytes = FieldBytes(field);
      if (!bytes) {
        return std::nullopt;
      }
      total += *bytes;
    }
    if (end == std::string_view::npos) {
      break;
    }
    body.remove_prefix(end + 1);
  }
  return total;
}

struct GeneratedShader {
  std::string entry;
  std::string text;
  [[nodiscard]] auto operator<=>(const GeneratedShader &) const = default;
};

constexpr std::string_view kExampleRecipes[]{
    "recipes/examples/arcane-circuit.json",
    "recipes/examples/resonant-ward.json", "recipes/examples/winterglass.json"};

constexpr std::string_view kImportTemplates[]{"fill.json", "bare.json"};

RenderBindingResolver InstanceBindings() {
  return [](const TextureValue &a_value, GeometryId) {
    return ValueBindings{[instance = a_value.instance](const ExternalSource &)
                             -> std::expected<std::string, std::string> {
                           return std::to_string(instance);
                         },
                         [instance = a_value.instance](NodeId a_node) {
                           return std::format("{}:{}", instance, a_node);
                         }};
  };
}

std::optional<Recipe> RecipeAt(const std::filesystem::path &a_path) {
  const LoadResult loaded =
      ParseRecipe(test::ReadFile(a_path), a_path.stem().string());
  Check(loaded.recipe.has_value() && !loaded.HasErrors(),
        std::format("{} parses", a_path.string()));
  return loaded.recipe;
}

std::optional<Recipe> ImportedFrom(const std::filesystem::path &a_template) {
  const std::optional<Recipe> templated = RecipeAt(a_template);
  const auto record = ParseEffectShaderRecord(
      test::ReadFile(test::Fixtures() / "efsh" / "SyntheticTiled.json"));
  if (!templated || !record) {
    return std::nullopt;
  }
  return ImportEffectShader(*record, *templated);
}

std::vector<Recipe> PlannedRecipes() {
  std::vector<Recipe> recipes;
  for (const std::string_view name : kImportTemplates) {
    if (std::optional<Recipe> imported =
            ImportedFrom(test::Templates() / name)) {
      recipes.push_back(std::move(*imported));
    }
  }
  for (const std::string_view path : kExampleRecipes) {
    if (std::optional<Recipe> example = RecipeAt(path)) {
      recipes.push_back(std::move(*example));
    }
  }
  return recipes;
}

std::optional<RenderPlan> PlanOf(const Recipe &a_recipe,
                                 const RecipeGraph &a_graph) {
  std::vector<RenderStackRequest> requests;
  for (std::size_t o = 0; o < a_recipe.outputs.size(); ++o) {
    if (const auto *surface = Get<SurfaceOutput>(a_recipe.outputs[o])) {
      requests.push_back(
          {&a_graph, surface, 1, PlacementId{o}, o, {SideRef{0}}});
    }
  }
  std::expected<RenderPlan, std::string> plan =
      BuildRenderPlan({}, requests, InstanceBindings());
  Check(plan.has_value(),
        std::format("recipe {} lowers into a plan", a_recipe.id));
  return plan ? std::optional{std::move(*plan)} : std::nullopt;
}

std::uint32_t ChannelBitsOf(const ChannelSet &a_channels) {
  return (a_channels.r ? 1u : 0u) | (a_channels.g ? 2u : 0u) |
         (a_channels.b ? 4u : 0u) | (a_channels.a ? 8u : 0u);
}

std::optional<StackShape> StackShapeOf(const CompositeStackStep &a_stack) {
  std::vector<const PlannedLayer *> layers;
  for (const PlannedLayer &layer : a_stack.layers) {
    layers.push_back(&layer);
  }
  const std::vector<std::size_t> fields = VisibleLayerFields(layers);
  std::vector<const LayerField *> used;
  for (const std::size_t field : fields) {
    if (field >= a_stack.fields.size()) {
      return std::nullopt;
    }
    used.push_back(&a_stack.fields[field]);
  }
  const auto packed = PackLayerFields(used);
  if (!packed) {
    return std::nullopt;
  }
  StackShape shape;
  shape.base = true;
  shape.code = CodeWithoutNumbers(packed->pack.code);
  shape.slots = InputTextureSlots(packed->pack.inputs);
  shape.segments = packed->pack.segments;
  for (const PlannedLayer &layer : a_stack.layers) {
    LayerShape out;
    if (const auto segment = SegmentIndexOf(fields, layer.source)) {
      out.source = SegmentRead{*segment};
    } else {
      out.source = SourceTexture{};
    }
    if (layer.mask) {
      if (const auto segment = SegmentIndexOf(fields, *layer.mask)) {
        out.mask = SegmentRead{*segment};
      } else {
        out.mask = MaskTexture{};
      }
    }
    out.blend = static_cast<std::uint32_t>(std::to_underlying(layer.blend));
    out.channels = ChannelBitsOf(layer.channels);
    shape.layers.push_back(out);
  }
  return shape;
}

std::set<GeneratedShader> GeneratedShadersOf(const RenderPlan &a_plan) {
  std::set<GeneratedShader> shaders;
  for (const RenderStep &step : a_plan.steps) {
    if (const auto *program = Get<EvaluateProgramStep>(step.kind)) {
      shaders.insert({std::string{kGeneratedProgramEntry},
                      GenerateProgramShader(program->program)});
    }
    const auto *stack = Get<CompositeStackStep>(step.kind);
    const std::optional<StackShape> shape =
        stack ? StackShapeOf(*stack) : std::nullopt;
    const auto text = shape ? GenerateStackShader(*shape)
                            : std::expected<std::string, std::string>{
                                  std::unexpected("no stack")};
    Check(!stack || text.has_value(),
          "every stack of the plan generates a shader");
    if (text) {
      shaders.insert({std::string{kGeneratedStackEntry}, *text});
    }
  }
  return shaders;
}

bool Compiles(const std::filesystem::path &a_scratch, std::string_view a_name,
              std::string_view a_entry, std::string_view a_profile,
              std::string_view a_text) {
  const std::filesystem::path source =
      a_scratch / std::format("{}.hlsl", a_name);
  const std::filesystem::path log = a_scratch / std::format("{}.log", a_name);
  if (!test::WriteFile(source, a_text)) {
    return false;
  }
  const std::string command = std::format(
      R"("{}" -HV 2018 -T {} -E {} -Fo "{}" "{}" > "{}" 2>&1)", BEEF_DXC,
      a_profile, a_entry, (a_scratch / std::format("{}.dxil", a_name)).string(),
      source.string(), log.string());
  const bool compiled = std::system(command.c_str()) == 0;
  if (!compiled) {
    std::printf("%s\n", test::ReadFile(log).c_str());
  }
  return compiled;
}

void GeneratedShadersCompile() {
  const std::filesystem::path scratch = test::ScratchDir("generated");
  std::set<GeneratedShader> shaders;
  for (const Recipe &recipe : PlannedRecipes()) {
    const RecipeGraph graph = RecipeGraph::Compile(recipe);
    if (const std::optional<RenderPlan> plan = PlanOf(recipe, graph)) {
      shaders.merge(GeneratedShadersOf(*plan));
    }
  }
  std::size_t programs = 0;
  std::size_t stacks = 0;
  std::size_t index = 0;
  for (const GeneratedShader &shader : shaders) {
    const std::string name = std::format("generated{}", index++);
    const bool compiled =
        Compiles(scratch, name, shader.entry, "ps_6_0",
                 std::string{kShaderSource} + "\n" + shader.text);
    Check(compiled,
          std::format("{} ({}) compiles with dxc", name, shader.entry));
    (shader.entry == kGeneratedStackEntry ? stacks : programs) += 1;
  }
  std::printf("compiled %zu program and %zu stack shaders\n", programs, stacks);
  Check(programs > 0 && stacks > 0,
        "the planned recipes generate program and stack shaders");
}

void ConstantBuffersMatchTheirStructs() {
  for (const ConstantBuffer &buffer : kConstantBuffers) {
    const std::optional<std::size_t> packed =
        PackedBytes(kShaderSource, buffer.name);
    Check(packed.has_value(),
          std::format("cbuffer {} parses as float4 fields", buffer.name));
    Check(packed == buffer.bytes,
          std::format("cbuffer {} holds {} bytes; its C++ struct {}",
                      buffer.name, packed.value_or(0), buffer.bytes));
  }
}

void EveryEntryPointCompiles() {
  const std::filesystem::path scratch = test::ScratchDir("shadersource");
  for (const EntryPoint &entry : kEntryPoints) {
    Check(
        Compiles(scratch, entry.name, entry.name, entry.profile, kShaderSource),
        std::format("{} compiles with dxc", entry.name));
  }
}
}

int main() {
  ConstantBuffersMatchTheirStructs();
  EveryEntryPointCompiles();
  GeneratedShadersCompile();
  return test::Finish("shadersource");
}
