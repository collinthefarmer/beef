// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlan.h"
#include "planners/ShaderConstants.h"
#include "planners/ShaderSource.h"
#include "planners/StackShader.h"
#include "recipe/Importer.h"
#include "recipe/RecipeGraph.h"
#include "test_support.h"

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Member {
  std::string name;
  std::string type;
  std::size_t offset = 0;
  [[nodiscard]] bool operator==(const Member &) const = default;
};

template <class T> constexpr std::string_view kHlslType = "unknown";
template <> constexpr std::string_view kHlslType<float> = "float";
template <> constexpr std::string_view kHlslType<std::uint32_t> = "uint";
template <> constexpr std::string_view kHlslType<ShaderBool> = "bool";
template <> constexpr std::string_view kHlslType<MapView> = "uint";
template <> constexpr std::string_view kHlslType<GroupDraw> = "uint";
template <> constexpr std::string_view kHlslType<Float2> = "float2";
template <> constexpr std::string_view kHlslType<Float3> = "float3";
template <> constexpr std::string_view kHlslType<Float4> = "float4";
template <> constexpr std::string_view kHlslType<Uint2> = "uint2";
template <> constexpr std::string_view kHlslType<Uint4> = "uint4";
template <>
constexpr std::string_view kHlslType<TextureRead> = "struct.TextureRead";
template <> constexpr std::string_view kHlslType<LayerMix> = "struct.LayerMix";
template <>
constexpr std::string_view kHlslType<NormalDetail> = "struct.NormalDetail";
template <>
constexpr std::string_view kHlslType<ProgramInputRow> =
    "struct.ProgramInputRow";
template <>
constexpr std::string_view kHlslType<StackLayer> = "struct.StackLayer";
template <>
constexpr std::string_view kHlslType<RippleFiringRow> =
    "struct.RippleFiringRow";
template <>
constexpr std::string_view kHlslType<GroupScore> = "struct.GroupScore";

template <class T> Member MemberOf(std::string_view a_name, std::size_t a_at) {
  if constexpr (std::is_array_v<T>) {
    return {std::format("{}[{}]", a_name, std::extent_v<T>),
            std::string{kHlslType<std::remove_extent_t<T>>}, a_at};
  } else {
    return {std::string{a_name}, std::string{kHlslType<T>}, a_at};
  }
}

#define BEEF_MEMBER(Record, field)                                             \
  MemberOf<decltype(Record::field)>(#field, offsetof(Record, field))

struct ExpectedBuffer {
  std::string_view name;
  std::string_view entry;
  ConstantSlot slot = ConstantSlot::kPass;
  std::size_t bytes = 0;
  std::vector<Member> members;
};

std::vector<ExpectedBuffer> ExpectedBuffers() {
  return {
      {"PassParams",
       "PSMain",
       ConstantSlot::kPass,
       sizeof(PassConstants),
       {BEEF_MEMBER(PassConstants, passSource),
        BEEF_MEMBER(PassConstants, passLayer),
        BEEF_MEMBER(PassConstants, passMode),
        BEEF_MEMBER(PassConstants, passBelowMip),
        BEEF_MEMBER(PassConstants, passMapChannel),
        BEEF_MEMBER(PassConstants, passMapView),
        BEEF_MEMBER(PassConstants, passMapMip)}},
      {"ProgramParams",
       "PSProgram",
       ConstantSlot::kProgram,
       sizeof(ProgramConstants),
       {BEEF_MEMBER(ProgramConstants, programCode),
        BEEF_MEMBER(ProgramConstants, programInputs),
        BEEF_MEMBER(ProgramConstants, programTextures),
        BEEF_MEMBER(ProgramConstants, programCount),
        BEEF_MEMBER(ProgramConstants, programVectorResult)}},
      {"RippleParams",
       "PSRipple",
       ConstantSlot::kRipple,
       sizeof(RippleConstants),
       {BEEF_MEMBER(RippleConstants, rippleFirings),
        BEEF_MEMBER(RippleConstants, rippleDirection),
        BEEF_MEMBER(RippleConstants, rippleFiringCount),
        BEEF_MEMBER(RippleConstants, rippleSpeed),
        BEEF_MEMBER(RippleConstants, rippleWidth),
        BEEF_MEMBER(RippleConstants, rippleDecay),
        BEEF_MEMBER(RippleConstants, rippleFrame),
        BEEF_MEMBER(RippleConstants, rippleDisc),
        BEEF_MEMBER(RippleConstants, rippleDirectional)}},
      {"GroupParams",
       "PSClusters",
       ConstantSlot::kGroup,
       sizeof(GroupConstants),
       {BEEF_MEMBER(GroupConstants, groups),
        BEEF_MEMBER(GroupConstants, groupAxisCentre),
        BEEF_MEMBER(GroupConstants, groupAxisSpread),
        BEEF_MEMBER(GroupConstants, groupLitCharts),
        BEEF_MEMBER(GroupConstants, groupDetail),
        BEEF_MEMBER(GroupConstants, groupCount),
        BEEF_MEMBER(GroupConstants, groupDraw),
        BEEF_MEMBER(GroupConstants, groupPicked),
        BEEF_MEMBER(GroupConstants, groupOddsWeight),
        BEEF_MEMBER(GroupConstants, groupRmaosMip),
        BEEF_MEMBER(GroupConstants, groupDiffuseMip),
        BEEF_MEMBER(GroupConstants, groupChartFilter)}},
      {"ReduceParams",
       "PSReduce",
       ConstantSlot::kReduce,
       sizeof(ReductionConstants),
       {BEEF_MEMBER(ReductionConstants, reduceKind),
        BEEF_MEMBER(ReductionConstants, reduceComponents),
        BEEF_MEMBER(ReductionConstants, reduceExtent),
        BEEF_MEMBER(ReductionConstants, reduceReadsField)}},
      {"StackParams",
       "PSStack",
       ConstantSlot::kStack,
       sizeof(StackConstants),
       {BEEF_MEMBER(StackConstants, stackLayers),
        BEEF_MEMBER(StackConstants, stackLayerCount),
        BEEF_MEMBER(StackConstants, stackHasBase),
        BEEF_MEMBER(StackConstants, stackBaseMip)}},
      {"DiffuseDetailParams",
       "PSDiffuseDetail",
       ConstantSlot::kDiffuseDetail,
       sizeof(DiffuseDetailConstants),
       {BEEF_MEMBER(DiffuseDetailConstants, diffuseDetailNormal),
        BEEF_MEMBER(DiffuseDetailConstants, diffuseDetailMip)}},
      {"BakeNoiseParams",
       "BakeNoisePS",
       ConstantSlot::kBakeNoise,
       sizeof(BakeNoiseConstants),
       {BEEF_MEMBER(BakeNoiseConstants, noiseOctaves),
        BEEF_MEMBER(BakeNoiseConstants, noiseSeed),
        BEEF_MEMBER(BakeNoiseConstants, noiseStyle)}},
      {"FilterParams",
       "PSFilter",
       ConstantSlot::kFilter,
       sizeof(FilterConstants),
       {BEEF_MEMBER(FilterConstants, filterOperation),
        BEEF_MEMBER(FilterConstants, filterTaps),
        BEEF_MEMBER(FilterConstants, filterVertical),
        BEEF_MEMBER(FilterConstants, filterKeepSeams),
        BEEF_MEMBER(FilterConstants, filterTargetSize),
        BEEF_MEMBER(FilterConstants, filterInputSize)}},
  };
}

struct ExpectedRecord {
  std::string_view type;
  std::vector<Member> members;
};

std::vector<ExpectedRecord> ExpectedRecords() {
  return {
      {kHlslType<TextureRead>,
       {BEEF_MEMBER(TextureRead, offset), BEEF_MEMBER(TextureRead, tile),
        BEEF_MEMBER(TextureRead, mip), BEEF_MEMBER(TextureRead, normalize),
        BEEF_MEMBER(TextureRead, channel), BEEF_MEMBER(TextureRead, options)}},
      {kHlslType<LayerMix>,
       {BEEF_MEMBER(LayerMix, color), BEEF_MEMBER(LayerMix, opacity),
        BEEF_MEMBER(LayerMix, blend), BEEF_MEMBER(LayerMix, writeChannels),
        BEEF_MEMBER(LayerMix, maskChannel), BEEF_MEMBER(LayerMix, options)}},
      {kHlslType<NormalDetail>,
       {BEEF_MEMBER(NormalDetail, fineMip),
        BEEF_MEMBER(NormalDetail, coarseMip),
        BEEF_MEMBER(NormalDetail, footprint),
        BEEF_MEMBER(NormalDetail, present)}},
      {kHlslType<ProgramInputRow>,
       {BEEF_MEMBER(ProgramInputRow, value),
        BEEF_MEMBER(ProgramInputRow, textureSlot)}},
      {kHlslType<StackLayer>,
       {BEEF_MEMBER(StackLayer, source), BEEF_MEMBER(StackLayer, mix),
        BEEF_MEMBER(StackLayer, sourceCode),
        BEEF_MEMBER(StackLayer, maskCode)}},
      {kHlslType<RippleFiringRow>,
       {BEEF_MEMBER(RippleFiringRow, origin),
        BEEF_MEMBER(RippleFiringRow, age)}},
      {kHlslType<GroupScore>,
       {BEEF_MEMBER(GroupScore, mean), BEEF_MEMBER(GroupScore, whitening),
        BEEF_MEMBER(GroupScore, id), BEEF_MEMBER(GroupScore, logScale),
        BEEF_MEMBER(GroupScore, padding)}},
  };
}

static_assert(sizeof(TextureRead) % 16 == 0 && sizeof(LayerMix) % 16 == 0 &&
              sizeof(NormalDetail) % 16 == 0 &&
              sizeof(ProgramInputRow) % 16 == 0 &&
              sizeof(StackLayer) % 16 == 0 &&
              sizeof(RippleFiringRow) % 16 == 0 &&
              sizeof(GroupScore) % 16 == 0);

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

struct Block {
  std::string type;
  std::vector<Member> members;
};

struct BufferLayout {
  std::vector<Member> members;
  std::size_t bytes = 0;
  std::optional<std::uint32_t> slot;
};

struct Disassembly {
  std::map<std::string, BufferLayout, std::less<>> buffers;
  std::map<std::string, std::vector<Member>, std::less<>> records;
};

std::string_view Trimmed(std::string_view a_text) {
  const auto first = a_text.find_first_not_of(" \t\r\n;");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = a_text.find_last_not_of(" \t\r\n;");
  return a_text.substr(first, last - first + 1);
}

std::optional<std::size_t> NumberAfter(std::string_view a_text,
                                       std::string_view a_label) {
  const auto at = a_text.find(a_label);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view rest = Trimmed(a_text.substr(at + a_label.size()));
  std::size_t value = 0;
  const auto parsed =
      std::from_chars(rest.data(), rest.data() + rest.size(), value);
  return parsed.ec == std::errc{} ? std::optional{value} : std::nullopt;
}

struct Declaration {
  std::string_view words;
  std::size_t offset = 0;
};

std::optional<Declaration> DeclarationOf(std::string_view a_line) {
  const auto marker = a_line.find("; Offset:");
  const std::optional<std::size_t> offset = NumberAfter(a_line, "; Offset:");
  if (marker == std::string_view::npos || !offset) {
    return std::nullopt;
  }
  return Declaration{Trimmed(a_line.substr(0, marker)), *offset};
}

std::pair<std::string, std::string> TypeAndName(std::string_view a_words) {
  const auto space = a_words.find_last_of(' ');
  if (space == std::string_view::npos) {
    return {};
  }
  return {std::string{Trimmed(a_words.substr(0, space))},
          std::string{Trimmed(a_words.substr(space + 1))}};
}

void ReadBinding(std::string_view a_line, Disassembly &a_out) {
  std::vector<std::string_view> words;
  for (const auto word : std::views::split(a_line, ' ')) {
    const std::string_view text{word.begin(), word.end()};
    if (!text.empty() && text != ";") {
      words.push_back(text);
    }
  }
  if (words.size() < 7 || words[1] != "cbuffer" ||
      !words[5].starts_with("cb")) {
    return;
  }
  const std::string_view digits = words[5].substr(2);
  std::uint32_t slot = 0;
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), slot);
  const auto found = a_out.buffers.find(words[0]);
  if (parsed.ec == std::errc{} && found != a_out.buffers.end()) {
    found->second.slot = slot;
  }
}

void CloseBlock(std::string_view a_line, std::vector<Block> &a_open,
                Disassembly &a_out) {
  const std::optional<Declaration> declared =
      DeclarationOf(Trimmed(a_line).substr(1));
  if (a_open.empty() || !declared) {
    return;
  }
  Block block = std::move(a_open.back());
  a_open.pop_back();
  const std::string name{Trimmed(declared->words)};
  if (a_open.empty()) {
    a_out.buffers[name] = {std::move(block.members),
                           NumberAfter(a_line, "Size:").value_or(0),
                           std::nullopt};
    return;
  }
  a_open.back().members.push_back({name, block.type, declared->offset});
  for (Member &member : block.members) {
    member.offset -= declared->offset;
  }
  a_out.records[block.type] = std::move(block.members);
}

Disassembly ParseDisassembly(std::string_view a_text) {
  Disassembly out;
  std::vector<Block> open;
  bool bindings = false;
  for (const auto piece : std::views::split(a_text, '\n')) {
    const std::string_view line{piece.begin(), piece.end()};
    const std::string_view words = Trimmed(line);
    if (words.starts_with("Resource Bindings:")) {
      bindings = true;
    } else if (bindings) {
      ReadBinding(line, out);
    } else if (words.starts_with("struct ")) {
      open.push_back({std::string{Trimmed(words.substr(7))}, {}});
    } else if (words.starts_with("}")) {
      CloseBlock(line, open, out);
    } else if (const auto declared = DeclarationOf(words);
               declared && !open.empty()) {
      auto [type, name] = TypeAndName(declared->words);
      open.back().members.push_back(
          {std::move(name), std::move(type), declared->offset});
    }
  }
  return out;
}

std::string Listing(const std::vector<Member> &a_members) {
  std::string text;
  for (const Member &member : a_members) {
    text +=
        std::format("\n    {} {} @{}", member.type, member.name, member.offset);
  }
  return text;
}

std::size_t RegisterBytes(std::size_t a_bytes) {
  return (a_bytes + 15) / 16 * 16;
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

std::optional<std::string> Compiles(const std::filesystem::path &a_scratch,
                                    std::string_view a_name,
                                    std::string_view a_entry,
                                    std::string_view a_profile,
                                    std::string_view a_text) {
  const std::filesystem::path source =
      a_scratch / std::format("{}.hlsl", a_name);
  const std::filesystem::path log = a_scratch / std::format("{}.log", a_name);
  const std::filesystem::path listing =
      a_scratch / std::format("{}.asm", a_name);
  if (!test::WriteFile(source, a_text)) {
    return std::nullopt;
  }
  const std::string command = std::format(
      R"("{}" -HV 2018 -T {} -E {} -Fo "{}" -Fc "{}" "{}" > "{}" 2>&1)",
      BEEF_DXC, a_profile, a_entry,
      (a_scratch / std::format("{}.dxil", a_name)).string(), listing.string(),
      source.string(), log.string());
  if (std::system(command.c_str()) != 0) {
    std::printf("%s\n", test::ReadFile(log).c_str());
    return std::nullopt;
  }
  return test::ReadFile(listing);
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
                 std::string{kShaderSource} + "\n" + shader.text)
            .has_value();
    Check(compiled,
          std::format("{} ({}) compiles with dxc", name, shader.entry));
    (shader.entry == kGeneratedStackEntry ? stacks : programs) += 1;
  }
  std::printf("compiled %zu program and %zu stack shaders\n", programs, stacks);
  Check(programs > 0 && stacks > 0,
        "the planned recipes generate program and stack shaders");
}

std::map<std::string, Disassembly, std::less<>> EveryEntryPointCompiles() {
  const std::filesystem::path scratch = test::ScratchDir("shadersource");
  std::map<std::string, Disassembly, std::less<>> listings;
  for (const EntryPoint &entry : kEntryPoints) {
    const std::optional<std::string> listing =
        Compiles(scratch, entry.name, entry.name, entry.profile, kShaderSource);
    Check(listing.has_value(), std::format("{} compiles with dxc", entry.name));
    if (listing) {
      listings[std::string{entry.name}] = ParseDisassembly(*listing);
    }
  }
  return listings;
}

void BufferMatchesItsStruct(const ExpectedBuffer &a_expected,
                            const Disassembly &a_listing) {
  const auto found = a_listing.buffers.find(a_expected.name);
  Check(
      found != a_listing.buffers.end(),
      std::format("{} declares cbuffer {}", a_expected.entry, a_expected.name));
  if (found == a_listing.buffers.end()) {
    return;
  }
  const BufferLayout &layout = found->second;
  Check(layout.members == a_expected.members,
        std::format("cbuffer {} matches its C++ struct member by member\n"
                    "  shader:{}\n  C++:{}",
                    a_expected.name, Listing(layout.members),
                    Listing(a_expected.members)));
  Check(RegisterBytes(layout.bytes) == a_expected.bytes,
        std::format("cbuffer {} holds {} bytes; its C++ struct {}",
                    a_expected.name, RegisterBytes(layout.bytes),
                    a_expected.bytes));
  Check(layout.slot == SlotOf(a_expected.slot),
        std::format("cbuffer {} sits at b{}; C++ binds it at b{}",
                    a_expected.name, layout.slot.value_or(99),
                    SlotOf(a_expected.slot)));
}

void RecordsMatchTheirStructs(
    const std::map<std::string, Disassembly, std::less<>> &a_listings) {
  for (const ExpectedRecord &record : ExpectedRecords()) {
    const std::vector<Member> *seen = nullptr;
    for (const auto &[entry, listing] : a_listings) {
      if (const auto found = listing.records.find(record.type);
          found != listing.records.end()) {
        seen = &found->second;
      }
    }
    Check(seen != nullptr,
          std::format("some entry point uses {}", record.type));
    if (seen) {
      Check(*seen == record.members,
            std::format("{} matches its C++ struct member by member\n"
                        "  shader:{}\n  C++:{}",
                        record.type, Listing(*seen), Listing(record.members)));
    }
  }
}

std::size_t DeclaredBuffers(std::string_view a_source) {
  std::size_t count = 0;
  for (auto at = a_source.find("cbuffer "); at != std::string_view::npos;
       at = a_source.find("cbuffer ", at + 1)) {
    ++count;
  }
  return count;
}

void ConstantBuffersMatchTheirStructs(
    const std::map<std::string, Disassembly, std::less<>> &a_listings) {
  const std::vector<ExpectedBuffer> expected = ExpectedBuffers();
  Check(DeclaredBuffers(kShaderSource) == expected.size(),
        "every cbuffer of the source has a C++ struct in the table");
  for (const ExpectedBuffer &buffer : expected) {
    const auto listing = a_listings.find(buffer.entry);
    if (listing != a_listings.end()) {
      BufferMatchesItsStruct(buffer, listing->second);
    }
  }
  RecordsMatchTheirStructs(a_listings);
}
}

int main() {
  ConstantBuffersMatchTheirStructs(EveryEntryPointCompiles());
  GeneratedShadersCompile();
  return test::Finish("shadersource");
}
