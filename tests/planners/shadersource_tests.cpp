// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ShaderSource.h"
#include "render/ShaderConstants.h"
#include "test_support.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>

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
    {"BakePS", "ps_6_0"},
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
  const std::filesystem::path source = scratch / "shaders.hlsl";
  Check(test::WriteFile(source, kShaderSource), "the shader source is written");
  for (const EntryPoint &entry : kEntryPoints) {
    const std::filesystem::path log =
        scratch / std::format("{}.log", entry.name);
    const std::string command =
        std::format(R"("{}" -HV 2018 -T {} -E {} -Fo "{}" "{}" > "{}" 2>&1)",
                    BEEF_DXC, entry.profile, entry.name,
                    (scratch / std::format("{}.dxil", entry.name)).string(),
                    source.string(), log.string());
    const bool compiled = std::system(command.c_str()) == 0;
    if (!compiled) {
      std::printf("%s\n", test::ReadFile(log).c_str());
    }
    Check(compiled, std::format("{} compiles with dxc", entry.name));
  }
}
}

int main() {
  ConstantBuffersMatchTheirStructs();
  EveryEntryPointCompiles();
  return test::Finish("shadersource");
}
