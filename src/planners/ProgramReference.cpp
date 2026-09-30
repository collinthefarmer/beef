// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ProgramReference.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
using Op = ProgramOpcode;

Vec3 Splat(float v) { return {v, v, v}; }
Vec3 Map(Vec3 a, float (*f)(float)) { return {f(a.x), f(a.y), f(a.z)}; }
Vec3 Map2(Vec3 a, Vec3 b, float (*f)(float, float)) {
  return {f(a.x, b.x), f(a.y, b.y), f(a.z, b.z)};
}
float Saturate(float v) {
  return std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f);
}
float SafeDiv(float a, float b) { return b == 0.0f ? 0.0f : a / b; }
float SafePow(float a, float b) {
  if (b == 0.0f || a == 1.0f)
    return 1.0f;
  float r = std::exp2(std::log2(std::fabs(a)) * b);
  if (a < 0.0f) {
    if (b != std::floor(b))
      return 0.0f;
    const float half = b * 0.5f;
    if (half != std::floor(half))
      r = -r;
  }
  return std::isfinite(r) ? r : 0.0f;
}
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Smoothstep(float lo, float hi, float x) {
  const float t = Saturate((x - lo) / (hi - lo));
  return t * t * (3.0f - 2.0f * t);
}
float Dot(Vec3 a, Vec3 b, bool two) {
  return a.x * b.x + a.y * b.y + (two ? 0.0f : a.z * b.z);
}
float Length(Vec3 a, bool two) { return std::sqrt(Dot(a, a, two)); }
float LookupAt(const LookupTable &table, float v) {
  const float position = Saturate(v) * 255.0f;
  const auto low = static_cast<std::size_t>(position);
  const auto high = std::min<std::size_t>(low + 1, table.size() - 1);
  return Lerp(table[low], table[high], position - static_cast<float>(low));
}
Vec3 Flag(bool v) { return Splat(v ? 1.0f : 0.0f); }
Vec3 Normalized(Vec3 a, bool two) {
  const float length = Length(a, two);
  return {SafeDiv(a.x, length), SafeDiv(a.y, length), SafeDiv(a.z, length)};
}
bool ReducesToScalar(ProgramOpcode opcode) {
  return opcode == Op::kLength || opcode == Op::kDistance || opcode == Op::kDot;
}

struct Operands {
  Vec3 a;
  Vec3 b;
  Vec3 d;
};

Vec3 PopOne(std::array<Vec3, kProgramStack> &stack, std::size_t &top) {
  if (top == 0 || top > stack.size())
    return Vec3{};
  --top;
  return stack[top];
}

Operands PopOperands(std::array<Vec3, kProgramStack> &stack, std::size_t &top,
                     std::size_t count) {
  Operands operands{};
  if (count >= 1)
    operands.a = PopOne(stack, top);
  if (count >= 2)
    operands.b = PopOne(stack, top);
  if (count >= 3)
    operands.d = PopOne(stack, top);
  return operands;
}

Vec3 ApplyOpcode(const ProgramInstruction &instruction,
                 const Operands &operands, const ProgramTexel &texel) {
  const Vec3 &a = operands.a;
  const Vec3 &b = operands.b;
  const Vec3 &d = operands.d;
  const bool two = instruction.components == 2;
  switch (instruction.opcode) {
  case Op::kNumber:
    return Splat(instruction.number);
  case Op::kMakeVec2:
    return {b.x, a.x, 0.0f};
  case Op::kMakeVec3:
    return {d.x, b.x, a.x};
  case Op::kInput:
    return instruction.index < texel.inputs.size()
               ? texel.inputs[instruction.index]
               : Vec3{};
  case Op::kLookup:
    return Splat(instruction.index < texel.lookups.size()
                     ? LookupAt(texel.lookups[instruction.index], a.x)
                     : 0.0f);
  case Op::kNeg:
    return {-a.x, -a.y, -a.z};
  case Op::kNot:
    return Flag(!(a.x > 0.0f));
  case Op::kAdd:
    return {b.x + a.x, b.y + a.y, b.z + a.z};
  case Op::kSub:
    return {b.x - a.x, b.y - a.y, b.z - a.z};
  case Op::kMul:
    return {b.x * a.x, b.y * a.y, b.z * a.z};
  case Op::kDiv:
    return Map2(b, a, SafeDiv);
  case Op::kLt:
    return Flag(b.x < a.x);
  case Op::kGt:
    return Flag(b.x > a.x);
  case Op::kLe:
    return Flag(b.x <= a.x);
  case Op::kGe:
    return Flag(b.x >= a.x);
  case Op::kEq:
    return Flag(b.x == a.x);
  case Op::kNe:
    return Flag(b.x != a.x);
  case Op::kAnd:
    return Flag(b.x > 0.0f && a.x > 0.0f);
  case Op::kOr:
    return Flag(b.x > 0.0f || a.x > 0.0f);
  case Op::kIf:
    return d.x > 0.0f ? b : a;
  case Op::kAbs:
    return Map(a, std::fabs);
  case Op::kMin:
    return Map2(b, a, [](float x, float y) { return std::min(x, y); });
  case Op::kMax:
    return Map2(b, a, [](float x, float y) { return std::max(x, y); });
  case Op::kClamp:
    return {std::min(std::max(d.x, b.x), a.x),
            std::min(std::max(d.y, b.y), a.y),
            std::min(std::max(d.z, b.z), a.z)};
  case Op::kSaturate:
    return Map(a, Saturate);
  case Op::kFloor:
    return Map(a, std::floor);
  case Op::kCeil:
    return Map(a, std::ceil);
  case Op::kFrac:
    return Map(a, [](float v) { return v - std::floor(v); });
  case Op::kSqrt:
    return Map(a, [](float v) { return std::sqrt(std::max(0.0f, v)); });
  case Op::kPow:
    return Map2(b, a, SafePow);
  case Op::kSin:
    return Map(a, std::sin);
  case Op::kCos:
    return Map(a, std::cos);
  case Op::kStep:
    return {a.x < b.x ? 0.0f : 1.0f, a.y < b.y ? 0.0f : 1.0f,
            a.z < b.z ? 0.0f : 1.0f};
  case Op::kSmoothstep:
    return {Smoothstep(d.x, b.x, a.x), Smoothstep(d.y, b.y, a.y),
            Smoothstep(d.z, b.z, a.z)};
  case Op::kLerp:
    return {Lerp(d.x, b.x, a.x), Lerp(d.y, b.y, a.y), Lerp(d.z, b.z, a.z)};
  case Op::kLength:
    return Splat(Length(a, two));
  case Op::kDistance:
    return Splat(Length({b.x - a.x, b.y - a.y, b.z - a.z}, two));
  case Op::kDot:
    return Splat(Dot(b, a, two));
  case Op::kCross:
    return {b.y * a.z - b.z * a.y, b.z * a.x - b.x * a.z,
            b.x * a.y - b.y * a.x};
  case Op::kNormalize:
    return Normalized(a, two);
  case Op::kQuantize:
    return QuantizeUnorm8(a);
  case Op::kSplat:
    return Splat(a.x);
  }
  return Normalized(a, two);
}

Vec3 KeepComponents(Vec3 result, const ProgramInstruction &instruction) {
  if (instruction.components == 2 && !ReducesToScalar(instruction.opcode))
    result.z = 0.0f;
  return result;
}
}
Vec3 QuantizeUnorm8(Vec3 value) {
  const auto channel = [](float v) {
    return std::nearbyint(Saturate(v) * 255.0f) / 255.0f;
  };
  return {channel(value.x), channel(value.y), channel(value.z)};
}
Vec3 EvaluateProgramOnCpu(const FieldProgram &program,
                          const ProgramTexel &texel) {
  return EvaluateProgramOnCpu(program.Instructions(), texel);
}
Vec3 EvaluateProgramOnCpu(std::span<const ProgramInstruction> code,
                          const ProgramTexel &texel) {
  std::array<Vec3, kProgramStack> stack{};
  std::size_t top = 0;
  for (const ProgramInstruction &instruction : code) {
    const Operands operands =
        PopOperands(stack, top, OpcodePops(instruction.opcode));
    const Vec3 result =
        KeepComponents(ApplyOpcode(instruction, operands, texel), instruction);
    if (top < stack.size())
      stack[top++] = result;
  }
  return top > 0 && top <= stack.size() ? stack[top - 1] : Vec3{};
}
}
