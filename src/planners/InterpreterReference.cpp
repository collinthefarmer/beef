// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InterpreterReference.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
using Op = InterpreterOpcode;

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
}
Vec3 QuantizeUnorm8(Vec3 value) {
  const auto channel = [](float v) {
    return std::nearbyint(Saturate(v) * 255.0f) / 255.0f;
  };
  return {channel(value.x), channel(value.y), channel(value.z)};
}
Vec3 EvaluateInterpreter(const InterpreterProgram &program,
                         const InterpreterTexel &texel) {
  return EvaluateInterpreter(program.Instructions(), texel);
}
Vec3 EvaluateInterpreter(std::span<const InterpreterInstruction> code,
                         const InterpreterTexel &texel) {
  std::array<Vec3, kInterpreterStack> stack{};
  std::size_t sp = 0;
  for (const auto &c : code) {
    Vec3 a{}, b{}, d{};
    const auto pops = InterpreterPops(c.opcode);
    if (pops >= 1 && sp > 0)
      a = stack[--sp];
    if (pops >= 2 && sp > 0)
      b = stack[--sp];
    if (pops >= 3 && sp > 0)
      d = stack[--sp];
    const bool two = c.components == 2;
    const auto flag = [](bool v) { return Splat(v ? 1.0f : 0.0f); };
    Vec3 r{};
    switch (c.opcode) {
    case Op::kNumber:
      r = Splat(c.number);
      break;
    case Op::kMakeVec2:
      r = {b.x, a.x, 0.0f};
      break;
    case Op::kMakeVec3:
      r = {d.x, b.x, a.x};
      break;
    case Op::kInput:
      r = c.index < texel.inputs.size() ? texel.inputs[c.index] : Vec3{};
      break;
    case Op::kLookup:
      r = Splat(c.index < texel.lookups.size()
                    ? LookupAt(texel.lookups[c.index], a.x)
                    : 0.0f);
      break;
    case Op::kNeg:
      r = {-a.x, -a.y, -a.z};
      break;
    case Op::kNot:
      r = flag(!(a.x > 0.0f));
      break;
    case Op::kAdd:
      r = {b.x + a.x, b.y + a.y, b.z + a.z};
      break;
    case Op::kSub:
      r = {b.x - a.x, b.y - a.y, b.z - a.z};
      break;
    case Op::kMul:
      r = {b.x * a.x, b.y * a.y, b.z * a.z};
      break;
    case Op::kDiv:
      r = Map2(b, a, SafeDiv);
      break;
    case Op::kLt:
      r = flag(b.x < a.x);
      break;
    case Op::kGt:
      r = flag(b.x > a.x);
      break;
    case Op::kLe:
      r = flag(b.x <= a.x);
      break;
    case Op::kGe:
      r = flag(b.x >= a.x);
      break;
    case Op::kEq:
      r = flag(b.x == a.x);
      break;
    case Op::kNe:
      r = flag(b.x != a.x);
      break;
    case Op::kAnd:
      r = flag(b.x > 0.0f && a.x > 0.0f);
      break;
    case Op::kOr:
      r = flag(b.x > 0.0f || a.x > 0.0f);
      break;
    case Op::kIf:
      r = d.x > 0.0f ? b : a;
      break;
    case Op::kAbs:
      r = Map(a, std::fabs);
      break;
    case Op::kMin:
      r = Map2(b, a, [](float x, float y) { return std::min(x, y); });
      break;
    case Op::kMax:
      r = Map2(b, a, [](float x, float y) { return std::max(x, y); });
      break;
    case Op::kClamp:
      r = {std::min(std::max(d.x, b.x), a.x), std::min(std::max(d.y, b.y), a.y),
           std::min(std::max(d.z, b.z), a.z)};
      break;
    case Op::kSaturate:
      r = Map(a, Saturate);
      break;
    case Op::kFloor:
      r = Map(a, std::floor);
      break;
    case Op::kCeil:
      r = Map(a, std::ceil);
      break;
    case Op::kFrac:
      r = Map(a, [](float v) { return v - std::floor(v); });
      break;
    case Op::kSqrt:
      r = Map(a, [](float v) { return std::sqrt(std::max(0.0f, v)); });
      break;
    case Op::kPow:
      r = Map2(b, a, SafePow);
      break;
    case Op::kSin:
      r = Map(a, std::sin);
      break;
    case Op::kCos:
      r = Map(a, std::cos);
      break;
    case Op::kStep:
      r = {a.x < b.x ? 0.0f : 1.0f, a.y < b.y ? 0.0f : 1.0f,
           a.z < b.z ? 0.0f : 1.0f};
      break;
    case Op::kSmoothstep:
      r = {Smoothstep(d.x, b.x, a.x), Smoothstep(d.y, b.y, a.y),
           Smoothstep(d.z, b.z, a.z)};
      break;
    case Op::kLerp:
      r = {Lerp(d.x, b.x, a.x), Lerp(d.y, b.y, a.y), Lerp(d.z, b.z, a.z)};
      break;
    case Op::kLength:
      r = Splat(Length(a, two));
      break;
    case Op::kDistance:
      r = Splat(Length({b.x - a.x, b.y - a.y, b.z - a.z}, two));
      break;
    case Op::kDot:
      r = Splat(Dot(b, a, two));
      break;
    case Op::kCross:
      r = {b.y * a.z - b.z * a.y, b.z * a.x - b.x * a.z, b.x * a.y - b.y * a.x};
      break;
    case Op::kQuantize:
      r = QuantizeUnorm8(a);
      break;
    case Op::kSplat:
      r = Splat(a.x);
      break;
    default: {
      const float length = Length(a, two);
      r = {SafeDiv(a.x, length), SafeDiv(a.y, length), SafeDiv(a.z, length)};
      break;
    }
    }
    if (two && c.opcode != Op::kLength && c.opcode != Op::kDistance &&
        c.opcode != Op::kDot)
      r.z = 0.0f;
    if (sp < stack.size())
      stack[sp++] = r;
  }
  return sp > 0 ? stack[sp - 1] : Vec3{};
}
}
