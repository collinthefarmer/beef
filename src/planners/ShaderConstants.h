// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/AreaPick.h"
#include "mesh/MaterialClusters.h"
#include "mesh/MaterialRegions.h"
#include "planners/FieldProgram.h"
#include "planners/StackShader.h"
#include "recipe/Reduction.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace BetterEnchantmentEffects {
struct Float2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Float3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Uint2 {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
};

struct Uint4 {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t z = 0;
  std::uint32_t w = 0;
};

using Float4 = std::array<float, 4>;

enum class ShaderBool : std::uint32_t { kNo = 0, kYes = 1 };

enum class ConstantSlot : std::uint32_t {
  kPass = 0,
  kProgram = 1,
  kRipple = 2,
  kGroup = 3,
  kReduce = 4,
  kStack = 5,
  kDiffuseDetail = 6,
  kBakeNoise = 7,
  kFilter = 8,
};
inline constexpr std::uint32_t kConstantSlotCount = 9;

inline constexpr std::uint32_t kReadMirrorU = 1u << 0;
inline constexpr std::uint32_t kReadMirrorV = 1u << 1;
inline constexpr std::uint32_t kReadTranspose = 1u << 2;
inline constexpr std::uint32_t kReadMeshSpace = 1u << 3;
inline constexpr std::uint32_t kReadNearest = 1u << 4;

inline constexpr std::uint32_t kLayerHasSource = 1u << 0;
inline constexpr std::uint32_t kLayerHasMask = 1u << 1;
inline constexpr std::uint32_t kLayerHasBelow = 1u << 2;
inline constexpr std::uint32_t kLayerHasCurve = 1u << 3;

enum class MapView : std::uint32_t { kChannel = 0, kSlope = 1, kIds = 2 };
enum class GroupDraw : std::uint32_t { kIds = 0, kPicked = 1, kPalette = 2 };

inline constexpr std::uint32_t kNoTextureSlot = 0xFFFFFFFFu;

inline constexpr std::uint32_t kInstructionOpByte = 0;
inline constexpr std::uint32_t kInstructionComponentsByte = 1;
inline constexpr std::uint32_t kInstructionPopsByte = 2;
inline constexpr std::uint32_t kInstructionIndexByte = 3;
inline constexpr std::size_t kInstructionsPerRow = 2;

struct ShaderWord {
  std::string_view name;
  std::uint32_t value = 0;
};

inline constexpr ShaderWord kShaderWords[]{
    {"kReadMirrorU", kReadMirrorU},
    {"kReadMirrorV", kReadMirrorV},
    {"kReadTranspose", kReadTranspose},
    {"kReadMeshSpace", kReadMeshSpace},
    {"kReadNearest", kReadNearest},
    {"kLayerHasSource", kLayerHasSource},
    {"kLayerHasMask", kLayerHasMask},
    {"kLayerHasBelow", kLayerHasBelow},
    {"kLayerHasCurve", kLayerHasCurve},
    {"kMapChannel", std::to_underlying(MapView::kChannel)},
    {"kMapSlope", std::to_underlying(MapView::kSlope)},
    {"kMapIds", std::to_underlying(MapView::kIds)},
    {"kGroupDrawIds", std::to_underlying(GroupDraw::kIds)},
    {"kGroupDrawPicked", std::to_underlying(GroupDraw::kPicked)},
    {"kGroupDrawPalette", std::to_underlying(GroupDraw::kPalette)},
    {"kNoTextureSlot", kNoTextureSlot},
    {"kInstructionOpByte", kInstructionOpByte},
    {"kInstructionComponentsByte", kInstructionComponentsByte},
    {"kInstructionPopsByte", kInstructionPopsByte},
    {"kInstructionIndexByte", kInstructionIndexByte},
};

struct TextureRead {
  Float2 offset;
  Float2 tile;
  float mip = 0.0f;
  float normalize = 1.0f;
  std::uint32_t channel = 0;
  std::uint32_t options = 0;
};

struct LayerMix {
  Float3 color{1.0f, 1.0f, 1.0f};
  float opacity = 1.0f;
  std::uint32_t blend = 0;
  std::uint32_t writeChannels = 15;
  std::uint32_t maskChannel = 0;
  std::uint32_t options = 0;
};

struct NormalDetail {
  float fineMip = 0.0f;
  float coarseMip = 0.0f;
  float footprint = 0.0f;
  ShaderBool present = ShaderBool::kNo;
};

struct alignas(16) PassConstants {
  TextureRead passSource;
  LayerMix passLayer;
  std::uint32_t passMode = 0;
  float passBelowMip = 0.0f;
  std::uint32_t passMapChannel = 0;
  MapView passMapView = MapView::kChannel;
  float passMapMip = 0.0f;
};

struct ProgramInputRow {
  Float3 value;
  std::uint32_t textureSlot = kNoTextureSlot;
};

struct alignas(16) ProgramConstants {
  Uint4 programCode[kProgramInstructions / kInstructionsPerRow];
  ProgramInputRow programInputs[kProgramInputs];
  TextureRead programTextures[kProgramTextures];
  std::uint32_t programCount = 0;
  ShaderBool programVectorResult = ShaderBool::kNo;
};
static_assert(kProgramInstructions % kInstructionsPerRow == 0 &&
              kProgramInputs <= 0xFF && kProgramLookups <= 0xFF);
static_assert(static_cast<int>(ProgramOpcode::kNumber) == 0 &&
              static_cast<int>(ProgramOpcode::kInput) == 3 &&
              static_cast<int>(ProgramOpcode::kIf) == 22 &&
              static_cast<int>(ProgramOpcode::kClamp) == 26 &&
              static_cast<int>(ProgramOpcode::kStep) == 35 &&
              static_cast<int>(ProgramOpcode::kLerp) == 37 &&
              static_cast<int>(ProgramOpcode::kLength) == 38 &&
              static_cast<int>(ProgramOpcode::kDistance) == 39 &&
              static_cast<int>(ProgramOpcode::kDot) == 40 &&
              static_cast<int>(ProgramOpcode::kCross) == 41 &&
              static_cast<int>(ProgramOpcode::kNormalize) == 42 &&
              static_cast<int>(ProgramOpcode::kQuantize) == 43 &&
              static_cast<int>(ProgramOpcode::kSplat) == 44);

struct StackLayer {
  TextureRead source;
  LayerMix mix;
  Uint2 sourceCode;
  Uint2 maskCode;
};

struct alignas(16) StackConstants {
  StackLayer stackLayers[kMaxStackLayers];
  std::uint32_t stackLayerCount = 0;
  ShaderBool stackHasBase = ShaderBool::kNo;
  float stackBaseMip = 0.0f;
};

struct RippleFiringRow {
  Float3 origin;
  float age = 0.0f;
};

inline constexpr std::size_t kRippleFiringRows = 8;
struct alignas(16) RippleConstants {
  RippleFiringRow rippleFirings[kRippleFiringRows];
  Float3 rippleDirection;
  std::uint32_t rippleFiringCount = 0;
  float rippleSpeed = 0.0f;
  float rippleWidth = 0.0f;
  float rippleDecay = 0.0f;
  float rippleFrame = 0.0f;
  ShaderBool rippleDisc = ShaderBool::kNo;
  ShaderBool rippleDirectional = ShaderBool::kNo;
};

inline constexpr std::size_t kGroupWhiteningRows = 9;
struct GroupScore {
  Float4 mean[2]{};
  Float4 whitening[kGroupWhiteningRows]{};
  std::uint32_t id = 0;
  float logScale = 0.0f;
  Uint2 padding;
};

inline constexpr std::size_t kLitChartRows = kChartSlots / 128;
struct alignas(16) GroupConstants {
  GroupScore groups[kMaxMaterialClusters];
  Float4 groupAxisCentre[2]{};
  Float4 groupAxisSpread[2]{};
  Uint4 groupLitCharts[kLitChartRows];
  NormalDetail groupDetail;
  std::uint32_t groupCount = 0;
  GroupDraw groupDraw = GroupDraw::kIds;
  std::uint32_t groupPicked = 0;
  float groupOddsWeight = 0.0f;
  float groupRmaosMip = 0.0f;
  float groupDiffuseMip = 0.0f;
  ShaderBool groupChartFilter = ShaderBool::kNo;
};
static_assert(kMaxMaterialClusters == 8 && kChartSlots % 128 == 0 &&
              kTexelAxes <= 8 && kWhiteningFloats <= kGroupWhiteningRows * 4);

struct alignas(16) DiffuseDetailConstants {
  NormalDetail diffuseDetailNormal;
  float diffuseDetailMip = 0.0f;
};

struct alignas(16) BakeNoiseConstants {
  std::uint32_t noiseOctaves = 0;
  std::uint32_t noiseSeed = 0;
  std::uint32_t noiseStyle = 0;
};

struct alignas(16) FilterConstants {
  std::uint32_t filterOperation = 0;
  std::uint32_t filterTaps = 0;
  ShaderBool filterVertical = ShaderBool::kNo;
  ShaderBool filterKeepSeams = ShaderBool::kNo;
  Uint2 filterTargetSize;
  Uint2 filterInputSize;
};
static_assert(static_cast<int>(FilterOperation::kBlur) == 0 &&
              static_cast<int>(FilterOperation::kGrow) == 1 &&
              static_cast<int>(FilterOperation::kShrink) == 2 &&
              static_cast<int>(FilterOperation::kEdge) == 3);

struct alignas(16) ReductionConstants {
  std::uint32_t reduceKind = 0;
  std::uint32_t reduceComponents = 0;
  Uint2 reduceExtent;
  ShaderBool reduceReadsField = ShaderBool::kNo;
};
static_assert(static_cast<int>(ReductionKind::kMean) == 0 &&
              static_cast<int>(ReductionKind::kSum) == 1 &&
              static_cast<int>(ReductionKind::kMinimum) == 2 &&
              static_cast<int>(ReductionKind::kMaximum) == 3);

[[nodiscard]] constexpr ShaderBool ShaderBoolOf(bool a_value) noexcept {
  return a_value ? ShaderBool::kYes : ShaderBool::kNo;
}

[[nodiscard]] constexpr std::uint32_t SlotOf(ConstantSlot a_slot) noexcept {
  return std::to_underlying(a_slot);
}

[[nodiscard]] constexpr std::uint32_t
InstructionByte(std::uint32_t a_value, std::uint32_t a_byte) noexcept {
  return std::min<std::uint32_t>(a_value, 0xFF) << (a_byte * 8);
}

[[nodiscard]] inline std::uint32_t
InstructionWord(const ProgramInstruction &a_instruction) noexcept {
  const auto opcode = static_cast<std::uint32_t>(a_instruction.opcode);
  const auto pops =
      static_cast<std::uint32_t>(OpcodePops(a_instruction.opcode));
  return InstructionByte(opcode, kInstructionOpByte) |
         InstructionByte(std::min<std::uint32_t>(a_instruction.components, 3),
                         kInstructionComponentsByte) |
         InstructionByte(pops, kInstructionPopsByte) |
         InstructionByte(a_instruction.index, kInstructionIndexByte);
}
}
