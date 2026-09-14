#pragma once

#include "mesh/MaterialClusters.h"
#include "recipe/Expression.h"

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
static_assert(static_cast<int>(Program::Op::kNumber) == 0 &&
              static_cast<int>(Program::Op::kRef) == 3 &&
              static_cast<int>(Program::Op::kIf) == 22 &&
              static_cast<int>(Program::Op::kClamp) == 26 &&
              static_cast<int>(Program::Op::kStep) == 35 &&
              static_cast<int>(Program::Op::kLerp) == 37);

struct alignas(16) RippleConstants {
  float firings[8][4];
  float shape[4];
  float misc[4];
};

struct alignas(16) ClassifyConstants {
  float centroidRmaos[kMaxClusters][4];
  float centroidLuma[kMaxClusters][4];
  float weights[4];
  float misc[4];
};
static_assert(kMaxClusters == 8 && sizeof(ClassifyConstants) % 16 == 0);
}
