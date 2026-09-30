// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/MaterialClusters.h"
#include "planners/InterpreterProgram.h"
#include "recipe/Reduction.h"

namespace BetterEnchantmentEffects {
struct alignas(16) LayerConstants {
  float offsetScale[4];
  float flags[4];
  float extra[4];
  float extra2[4];
  float layer[4];
  float layerColor[4];
  float layerMask[4];
  float layerCurve[4];
};

struct alignas(16) ProgramConstants {
  float code[256][4];
  float refs[16][4];
  float refValues[16][4];
  float texParams[8][4];
  float texTransform[8][4];
  float texFlags[8][4];
  float misc[4];
};
static_assert(sizeof(ProgramConstants) % 16 == 0);
static_assert(static_cast<int>(InterpreterOpcode::kNumber) == 0 &&
              static_cast<int>(InterpreterOpcode::kInput) == 3 &&
              static_cast<int>(InterpreterOpcode::kIf) == 22 &&
              static_cast<int>(InterpreterOpcode::kClamp) == 26 &&
              static_cast<int>(InterpreterOpcode::kStep) == 35 &&
              static_cast<int>(InterpreterOpcode::kLerp) == 37 &&
              static_cast<int>(InterpreterOpcode::kLength) == 38 &&
              static_cast<int>(InterpreterOpcode::kDistance) == 39 &&
              static_cast<int>(InterpreterOpcode::kDot) == 40 &&
              static_cast<int>(InterpreterOpcode::kCross) == 41 &&
              static_cast<int>(InterpreterOpcode::kNormalize) == 42 &&
              static_cast<int>(InterpreterOpcode::kQuantize) == 43 &&
              static_cast<int>(InterpreterOpcode::kSplat) == 44);

struct alignas(16) StackConstants {
  float offsetScale[8][4];
  float flags[8][4];
  float layer[8][4];
  float color[8][4];
  float mask[8][4];
  float misc[4];
};
static_assert(sizeof(StackConstants) % 16 == 0);

struct alignas(16) RippleConstants {
  float firings[8][4];
  float shape[4];
  float misc[4];
  float direction[4];
};

struct alignas(16) ClusterConstants {
  float centroidRmaos[kMaxMaterialClusters][4];
  float centroidLuma[kMaxMaterialClusters][4];
  float centroidDiffuse[kMaxMaterialClusters][4];
  float weights[4];
  float misc[4];
};
static_assert(kMaxMaterialClusters == 8 && sizeof(ClusterConstants) % 16 == 0);

struct alignas(16) ReductionConstants {
  std::uint32_t shape[4];
  std::uint32_t flags[4];
};
static_assert(sizeof(ReductionConstants) == 32 &&
              static_cast<int>(ReductionKind::kMean) == 0 &&
              static_cast<int>(ReductionKind::kSum) == 1 &&
              static_cast<int>(ReductionKind::kMinimum) == 2 &&
              static_cast<int>(ReductionKind::kMaximum) == 3);
}
