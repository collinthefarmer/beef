// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ShaderSource.h"

#include "mesh/MaterialClusters.h"
#include "planners/ProgramShader.h"
#include "planners/ShaderConstants.h"
#include "planners/ShaderFiles.h"
#include "recipe/ExpressionMath.h"
#include "recipe/Words.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
namespace {
using GeneratedText = std::string (*)();

struct ShaderPart {
  std::string_view file;
  GeneratedText generated = nullptr;
};

struct TextureSwitch {
  std::string_view signature;
  std::string_view prologue;
  std::string_view statement;
  std::string_view epilogue;
  std::string_view texture;
  std::uint32_t count = 0;
};

constexpr TextureSwitch kTextureSwitches[]{
    {"float4 SampleProgramTexture(int slot, float2 uv, float mip)", "",
     "return {}.SampleLevel(linearSampler, uv, mip);", "", "programTexture",
     kProgramTextures},
    {"float2 ProgramTextureSize(int slot)", "\tfloat w = 0, h = 0;\n",
     "{}.GetDimensions(w, h); break;", "\treturn float2(w, h);\n",
     "programTexture", kProgramTextures},
    {"float LoadLookup(int slot, int at)", "",
     "return {}.Load(int3(at, 0, 0)).x;", "", "programLookup", kProgramLookups},
    {"float4 SampleStackSource(int slot, float2 uv, float mip)", "",
     "return {}.SampleLevel(linearSampler, uv, mip);", "", "stackSourceTexture",
     kMaxStackLayers},
    {"float4 SampleStackMask(int slot, float2 uv)", "",
     "return {}.SampleLevel(linearSampler, uv, 0);", "", "stackMaskTexture",
     kMaxStackLayers},
};

std::string TitleCase(std::string_view a_word) {
  std::string out{a_word};
  if (!out.empty())
    out[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

std::string WordLine(std::string_view a_name, std::uint32_t a_value) {
  return std::format("static const uint {} = {}u;\n", a_name, a_value);
}

std::string WordsText() {
  std::string text;
  for (const ShaderWord &word : kShaderWords)
    text += WordLine(word.name, word.value);
  for (const BlendSpec &blend : kBlends)
    text += WordLine("kBlend" + TitleCase(blend.name), blend.shaderMode);
  return text;
}

std::string ShuffleText() {
  constexpr auto kTotal = static_cast<std::uint32_t>(kMaxShuffleTotal);
  std::string text =
      WordLine("kMaxShuffleTotal", kTotal) +
      WordLine("kShuffleOutside", static_cast<std::uint32_t>(kShuffleOutside));
  text += std::format("static const uint kPrimeAtLeast[{}] = {{", kTotal + 1);
  for (std::uint32_t n = 0; n <= kTotal; ++n)
    text += std::format("{}{}u", n == 0 ? "" : ", ", PrimeAtLeast(n));
  return text + "};\n";
}

std::string PaletteText() {
  std::string text = std::format("static const float3 kIdPalette[{}] = {{",
                                 kClusterPalette.size());
  for (std::size_t k = 0; k < kClusterPalette.size(); ++k) {
    const Vec3 &colour = kClusterPalette[k];
    text += std::format("{}float3({}, {}, {})", k == 0 ? "" : ", ", colour.x,
                        colour.y, colour.z);
  }
  return text + "};\n";
}

std::string TextureName(const ShaderTexture &a_texture, std::uint32_t a_k) {
  return a_texture.count == 1 ? std::string{a_texture.name}
                              : std::format("{}{}", a_texture.name, a_k);
}

std::string ResourcesText() {
  std::string text = "SamplerState linearSampler : register(s0);\n";
  for (const ShaderTexture &texture : kShaderTextures)
    for (std::uint32_t k = 0; k < texture.count; ++k)
      text += std::format("Texture2D {} : register(t{});\n",
                          TextureName(texture, k), texture.slot + k);
  return text;
}

std::string CaseStatement(std::string_view a_statement,
                          std::string_view a_texture) {
  std::string text{a_statement};
  const auto at = text.find("{}");
  if (at != std::string::npos)
    text.replace(at, 2, a_texture);
  return text;
}

std::string SwitchText(const TextureSwitch &a_switch) {
  std::string text = std::format("\n{}\n{{\n{}\tswitch (slot) {{\n",
                                 a_switch.signature, a_switch.prologue);
  for (std::uint32_t k = 0; k < a_switch.count; ++k) {
    const std::string label =
        k + 1 < a_switch.count ? std::format("case {}", k) : "default";
    const std::string texture = std::format("{}{}", a_switch.texture, k);
    text += std::format("\t{}: {}\n", label,
                        CaseStatement(a_switch.statement, texture));
  }
  return text + "\t}\n" + std::string{a_switch.epilogue} + "}\n";
}

std::string SwitchesText() {
  std::string text;
  for (const TextureSwitch &textureSwitch : kTextureSwitches)
    text += SwitchText(textureSwitch);
  return text;
}

std::string OpcodeText() {
  return "float3 RunOpcode(int op, float3 a, float3 b, float3 d, float number, "
         "int idx, int components, float2 uv)\n{\n\tfloat3 r = 0;\n" +
         InterpreterSwitch() + InterpreterZRule() + "\treturn r;\n}\n";
}

constexpr ShaderPart kShaderParts[]{
    {"words", WordsText},
    {"shuffle", ShuffleText},
    {"palette", PaletteText},
    {"resources", ResourcesText},
    {"switches", SwitchesText},
    {"Common"},
    {"Pass"},
    {"ProgramOps"},
    {"opcodes", OpcodeText},
    {"Program"},
    {"Bake"},
    {"Noise"},
    {"Dilate"},
    {"Filter"},
    {"Reduce"},
    {"Ripple"},
    {"Stack"},
    {"Material"},
    {"Groups"},
};

std::string_view FileText(std::string_view a_name) {
  const std::span<const ShaderFile> files = ShaderFiles();
  const auto found = std::ranges::find(files, a_name, &ShaderFile::name);
  return found == files.end() ? std::string_view{} : found->text;
}

std::string JoinedSource() {
  std::string text;
  for (const ShaderPart &part : kShaderParts) {
    text +=
        part.generated ? part.generated() : std::string{FileText(part.file)};
    text += "\n";
  }
  return text;
}

const std::string kShaderSourceStorage = JoinedSource();
}

extern const char *const kShaderSource = kShaderSourceStorage.c_str();
}
