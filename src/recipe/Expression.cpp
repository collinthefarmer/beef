#include "recipe/Expression.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kEpsilon = 1e-6f;

struct Function {
  std::string_view name;
  Program::Op op;
  int arity;
};
constexpr std::array<Function, 16> kFunctions{{
    {"abs", Program::Op::kAbs, 1},
    {"min", Program::Op::kMin, 2},
    {"max", Program::Op::kMax, 2},
    {"clamp", Program::Op::kClamp, 3},
    {"saturate", Program::Op::kSaturate, 1},
    {"floor", Program::Op::kFloor, 1},
    {"ceil", Program::Op::kCeil, 1},
    {"frac", Program::Op::kFrac, 1},
    {"sqrt", Program::Op::kSqrt, 1},
    {"pow", Program::Op::kPow, 2},
    {"sin", Program::Op::kSin, 1},
    {"cos", Program::Op::kCos, 1},
    {"step", Program::Op::kStep, 2},
    {"smoothstep", Program::Op::kSmoothstep, 3},
    {"lerp", Program::Op::kLerp, 3},
    {"if", Program::Op::kIf, 3},
}};

template <class F> Value Unary(const Value &a, F a_f) noexcept {
  return Match(
      a, [&](float x) -> Value { return a_f(x); },
      [&](const Vec2 &v) -> Value { return Vec2{a_f(v.x), a_f(v.y)}; },
      [&](const Vec3 &v) -> Value {
        return Vec3{a_f(v.x), a_f(v.y), a_f(v.z)};
      });
}

template <class F>
Value Binary(const Value &a, const Value &b, F a_f) noexcept {
  const auto ta = TypeOf(a), tb = TypeOf(b);
  if (ta == ValueType::kScalar && tb == ValueType::kScalar) {
    return a_f(std::get<float>(a), std::get<float>(b));
  }
  const auto vec = ta == ValueType::kScalar ? tb : ta;
  if (ta != ValueType::kScalar && tb != ValueType::kScalar && ta != tb) {
    return 0.0f;
  }
  if (vec == ValueType::kVec2) {
    const auto x = AsVec2(a), y = AsVec2(b);
    return Vec2{a_f(x.x, y.x), a_f(x.y, y.y)};
  }
  const auto x = AsVec3(a), y = AsVec3(b);
  return Vec3{a_f(x.x, y.x), a_f(x.y, y.y), a_f(x.z, y.z)};
}

template <class F>
Value Ternary(const Value &a, const Value &b, const Value &c, F a_f) noexcept {
  const ValueType types[]{TypeOf(a), TypeOf(b), TypeOf(c)};
  ValueType vec = ValueType::kScalar;
  for (const auto t : types) {
    if (t == ValueType::kScalar) {
      continue;
    }
    if (vec != ValueType::kScalar && vec != t) {
      return 0.0f;
    }
    vec = t;
  }
  if (vec == ValueType::kScalar) {
    return a_f(std::get<float>(a), std::get<float>(b), std::get<float>(c));
  }
  if (vec == ValueType::kVec2) {
    const auto x = AsVec2(a), y = AsVec2(b), z = AsVec2(c);
    return Vec2{a_f(x.x, y.x, z.x), a_f(x.y, y.y, z.y)};
  }
  const auto x = AsVec3(a), y = AsVec3(b), z = AsVec3(c);
  return Vec3{a_f(x.x, y.x, z.x), a_f(x.y, y.y, z.y), a_f(x.z, y.z, z.z)};
}

float Smoothstep(float lo, float hi, float x) noexcept {
  if (std::fabs(hi - lo) <= kEpsilon) {
    return x < lo ? 0.0f : 1.0f;
  }
  const float t = Clamp01((x - lo) / (hi - lo));
  return t * t * (3.0f - 2.0f * t);
}

float Truth(const Value &a) noexcept {
  return AsScalar(a) > 0.0f ? 1.0f : 0.0f;
}

std::expected<ValueType, std::string> Join(ValueType a, ValueType b,
                                           std::string_view a_what) {
  if (a == ValueType::kScalar) {
    return b;
  }
  if (b == ValueType::kScalar || a == b) {
    return a;
  }
  return std::unexpected(
      std::format("'{}' mixes {} with {}", a_what, Name(a), Name(b)));
}
}

class ExpressionParser {
public:
  explicit ExpressionParser(std::string_view a_text) : text_(a_text) {}

  std::expected<Program, std::string> Run() {
    if (text_.size() > kMaxExpressionLength) {
      return std::unexpected(
          std::format("longer than {} characters", kMaxExpressionLength));
    }
    Skip();
    if (At() == '\0') {
      return std::unexpected("empty expression");
    }
    if (const auto err = ParseOr()) {
      return std::unexpected(*err);
    }
    Skip();
    if (At() == '^') {
      return std::unexpected(std::format(
          "'^' at {}: there is no power operator, use pow(a, b)", pos_));
    }
    if (At() != '\0') {
      return std::unexpected(std::format("unexpected '{}' at {}", At(), pos_));
    }
    return std::move(out_);
  }

private:
  using Op = Program::Op;
  using Error = std::optional<std::string>;

  [[nodiscard]] char At(std::size_t a_ahead = 0) const noexcept {
    const auto i = pos_ + a_ahead;
    return i < text_.size() ? text_[i] : '\0';
  }

  void Skip() noexcept {
    while (std::isspace(static_cast<unsigned char>(At()))) {
      ++pos_;
    }
  }

  bool Take(std::string_view a_symbol) noexcept {
    Skip();
    if (text_.substr(pos_).starts_with(a_symbol)) {
      pos_ += a_symbol.size();
      return true;
    }
    return false;
  }

  bool TakeWord(std::string_view a_word) noexcept {
    Skip();
    if (!text_.substr(pos_).starts_with(a_word)) {
      return false;
    }
    const char after = At(a_word.size());
    if (std::isalnum(static_cast<unsigned char>(after)) || after == '_') {
      return false;
    }
    pos_ += a_word.size();
    return true;
  }

  Error Emit(Op a_op, float a_number = 0.0f, std::uint32_t a_index = 0) {
    if (out_.code_.size() >= kMaxExpressionOps) {
      return std::format("more than {} operations", kMaxExpressionOps);
    }
    out_.code_.push_back({a_op, a_number, a_index});
    return std::nullopt;
  }

  struct Depth {
    std::size_t &counter;
    explicit Depth(std::size_t &a_counter) : counter(a_counter) { ++counter; }
    ~Depth() { --counter; }
    Depth(const Depth &) = delete;
    Depth &operator=(const Depth &) = delete;
    Depth(Depth &&) = delete;
    Depth &operator=(Depth &&) = delete;
  };

  Error ParseOr() {
    if (auto e = ParseAnd()) {
      return e;
    }
    while (TakeWord("or")) {
      if (auto e = ParseAnd()) {
        return e;
      }
      if (auto e = Emit(Op::kOr)) {
        return e;
      }
    }
    return std::nullopt;
  }

  Error ParseAnd() {
    if (auto e = ParseCompare()) {
      return e;
    }
    while (TakeWord("and")) {
      if (auto e = ParseCompare()) {
        return e;
      }
      if (auto e = Emit(Op::kAnd)) {
        return e;
      }
    }
    return std::nullopt;
  }

  Error ParseCompare() {
    if (auto e = ParseAdd()) {
      return e;
    }
    struct Cmp {
      std::string_view symbol;
      Op op;
    };
    static constexpr Cmp kCompares[]{{"<=", Op::kLe}, {">=", Op::kGe},
                                     {"==", Op::kEq}, {"!=", Op::kNe},
                                     {"<", Op::kLt},  {">", Op::kGt}};
    for (const auto &c : kCompares) {
      if (Take(c.symbol)) {
        if (auto e = ParseAdd()) {
          return e;
        }
        return Emit(c.op);
      }
    }
    return std::nullopt;
  }

  Error ParseAdd() {
    if (auto e = ParseMul()) {
      return e;
    }
    for (;;) {
      Skip();
      const char c = At();
      if (c != '+' && c != '-') {
        return std::nullopt;
      }
      ++pos_;
      if (auto e = ParseMul()) {
        return e;
      }
      if (auto e = Emit(c == '+' ? Op::kAdd : Op::kSub)) {
        return e;
      }
    }
  }

  Error ParseMul() {
    if (auto e = ParseUnary()) {
      return e;
    }
    for (;;) {
      Skip();
      const char c = At();
      if (c != '*' && c != '/') {
        return std::nullopt;
      }
      ++pos_;
      if (auto e = ParseUnary()) {
        return e;
      }
      if (auto e = Emit(c == '*' ? Op::kMul : Op::kDiv)) {
        return e;
      }
    }
  }

  Error ParseUnary() {
    if (depth_ >= kMaxExpressionDepth) {
      return std::format("nested deeper than {}", kMaxExpressionDepth);
    }
    Depth depth{depth_};
    Skip();
    if (At() == '-') {
      ++pos_;
      if (auto e = ParseUnary()) {
        return e;
      }
      return Emit(Op::kNeg);
    }
    if (TakeWord("not")) {
      if (auto e = ParseUnary()) {
        return e;
      }
      return Emit(Op::kNot);
    }
    if (At() == '^') {
      return std::format("'^' at {}: there is no power operator, use pow(a, b)",
                         pos_);
    }
    return ParseAtom();
  }

  Error ParseAtom() {
    Skip();
    const char c = At();
    if (c == '(') {
      ++pos_;
      if (auto e = ParseOr()) {
        return e;
      }
      if (!Take(")")) {
        return std::format("expected ')' at {}", pos_);
      }
      return std::nullopt;
    }
    if (c == '[') {
      return ParseVector();
    }
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(At(1))))) {
      return ParseNumber();
    }
    if (c == '@') {
      return ParseReference();
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      return ParseWord();
    }
    if (c == '\0') {
      return std::string{"unexpected end of expression"};
    }
    return std::format("unexpected '{}' at {}", c, pos_);
  }

  Error ParseVector() {
    ++pos_;
    int count = 0;
    for (;;) {
      if (auto e = ParseOr()) {
        return e;
      }
      ++count;
      if (Take("]")) {
        break;
      }
      if (!Take(",") || count >= 3) {
        return std::format("a vector is [a, b] or [a, b, c], at {}", pos_);
      }
    }
    if (count < 2) {
      return std::format("a vector needs two or three components, at {}", pos_);
    }
    return Emit(count == 2 ? Op::kMakeVec2 : Op::kMakeVec3);
  }

  Error ParseNumber() {
    float value = 0.0f;
    const auto r = std::from_chars(text_.data() + pos_,
                                   text_.data() + text_.size(), value);
    if (r.ec != std::errc{} || !std::isfinite(value)) {
      return std::format("bad number at {}", pos_);
    }
    pos_ = static_cast<std::size_t>(r.ptr - text_.data());
    return Emit(Op::kNumber, value);
  }

  std::string ReadName() {
    const auto start = pos_;
    while (std::isalnum(static_cast<unsigned char>(At())) || At() == '_') {
      ++pos_;
    }
    return std::string{text_.substr(start, pos_ - start)};
  }

  Error ParseReference() {
    ++pos_;
    if (!std::isalpha(static_cast<unsigned char>(At())) && At() != '_') {
      return std::format("'@' must be followed by a name, at {}", pos_);
    }
    const auto name = ReadName();
    if (Take("(")) {
      if (auto e = ParseOr()) {
        return e;
      }
      if (!Take(")")) {
        return std::format("expected ')' after the curve argument at {}", pos_);
      }
      return Emit(Op::kCurve, 0.0f, Intern(out_.curves_, name));
    }
    return Emit(Op::kRef, 0.0f, Intern(out_.refs_, name));
  }

  static std::uint32_t Intern(std::vector<std::string> &a_names,
                              const std::string &a_name) {
    const auto it = std::ranges::find(a_names, a_name);
    if (it != a_names.end()) {
      return static_cast<std::uint32_t>(it - a_names.begin());
    }
    a_names.push_back(a_name);
    return static_cast<std::uint32_t>(a_names.size() - 1);
  }

  Error ParseWord() {
    const auto name = ReadName();
    if (name == "x") {
      out_.usesX_ = true;
      return Emit(Op::kX);
    }
    if (name == "mean") {
      out_.usesMean_ = true;
      return Emit(Op::kMean);
    }
    if (name == "time") {
      out_.usesTime_ = true;
      return Emit(Op::kTime);
    }
    if (name == "pi") {
      return Emit(Op::kNumber, std::numbers::pi_v<float>);
    }
    const auto fn = std::ranges::find(kFunctions, name, &Function::name);
    if (fn == kFunctions.end()) {
      return std::format("unknown name '{}' at {}; rows are written @{}", name,
                         pos_, name);
    }
    if (!Take("(")) {
      return std::format("'{}' is a function and needs '('", name);
    }
    for (int i = 0; i < fn->arity; ++i) {
      if (i > 0 && !Take(",")) {
        return std::format("'{}' takes {} arguments", name, fn->arity);
      }
      if (auto e = ParseOr()) {
        return e;
      }
    }
    if (!Take(")")) {
      return std::format("'{}' takes {} arguments", name, fn->arity);
    }
    return Emit(fn->op);
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t depth_ = 0;
  Program out_;
};

std::expected<Program, std::string> Program::Parse(std::string_view a_text) {
  return ExpressionParser{a_text}.Run();
}

std::expected<Program, std::string> ParseCurve(std::string_view a_text) {
  auto program = Program::Parse(a_text);
  if (!program) {
    return program;
  }
  if (!program->References().empty()) {
    return std::unexpected(
        std::format("a curve is a function of x and reads no rows ('@{}')",
                    program->References()[0]));
  }
  if (!program->Curves().empty()) {
    return std::unexpected("a curve cannot call another curve");
  }
  return program;
}

std::expected<ValueType, std::string> Program::Check(const RefTyper &a_types,
                                                     ValueType a_xType) const {
  std::vector<ValueType> stack;
  stack.reserve(16);
  const auto pop = [&]() {
    const auto t = stack.empty() ? ValueType::kScalar : stack.back();
    if (!stack.empty()) {
      stack.pop_back();
    }
    return t;
  };
  for (const auto &node : code_) {
    switch (node.op) {
    case Op::kNumber:
    case Op::kTime:
    case Op::kMean:
      stack.push_back(ValueType::kScalar);
      break;
    case Op::kX:
      stack.push_back(a_xType);
      break;
    case Op::kMakeVec2:
    case Op::kMakeVec3: {
      const int n = node.op == Op::kMakeVec2 ? 2 : 3;
      for (int i = 0; i < n; ++i) {
        if (pop() != ValueType::kScalar) {
          return std::unexpected("vector components must be scalars");
        }
      }
      stack.push_back(n == 2 ? ValueType::kVec2 : ValueType::kVec3);
      break;
    }
    case Op::kRef: {
      const auto &name = refs_[node.index];
      const auto t = a_types(name);
      if (!t) {
        return std::unexpected(std::format("unknown row '@{}'", name));
      }
      stack.push_back(*t);
      break;
    }
    case Op::kCurve:
      if (pop() != ValueType::kScalar) {
        return std::unexpected(
            std::format("curve '@{}' takes a scalar", curves_[node.index]));
      }
      stack.push_back(ValueType::kScalar);
      break;
    case Op::kNeg:
    case Op::kAbs:
    case Op::kSaturate:
    case Op::kFloor:
    case Op::kCeil:
    case Op::kFrac:
    case Op::kSqrt:
    case Op::kSin:
    case Op::kCos:
      stack.push_back(pop());
      break;
    case Op::kNot:
    case Op::kLt:
    case Op::kGt:
    case Op::kLe:
    case Op::kGe:
    case Op::kEq:
    case Op::kNe:
    case Op::kAnd:
    case Op::kOr: {
      const int n = node.op == Op::kNot ? 1 : 2;
      for (int i = 0; i < n; ++i) {
        if (pop() != ValueType::kScalar) {
          return std::unexpected("comparisons and logic take scalars");
        }
      }
      stack.push_back(ValueType::kScalar);
      break;
    }
    case Op::kAdd:
    case Op::kSub:
    case Op::kMul:
    case Op::kDiv:
    case Op::kMin:
    case Op::kMax:
    case Op::kPow:
    case Op::kStep: {
      const auto b = pop(), a = pop();
      auto t = Join(a, b, "operator");
      if (!t) {
        return std::unexpected(t.error());
      }
      stack.push_back(*t);
      break;
    }
    case Op::kClamp:
    case Op::kSmoothstep:
    case Op::kLerp: {
      const auto c = pop(), b = pop(), a = pop();
      auto t = Join(a, b, "function");
      if (!t) {
        return std::unexpected(t.error());
      }
      t = Join(*t, c, "function");
      if (!t) {
        return std::unexpected(t.error());
      }
      stack.push_back(*t);
      break;
    }
    case Op::kIf: {
      const auto b = pop(), a = pop(), c = pop();
      if (c != ValueType::kScalar) {
        return std::unexpected("if() takes a scalar condition");
      }
      auto t = Join(a, b, "if");
      if (!t) {
        return std::unexpected(t.error());
      }
      stack.push_back(*t);
      break;
    }
    }
  }
  return stack.empty() ? ValueType::kScalar : stack.back();
}

Value Program::Evaluate(const Inputs &a_inputs) const noexcept {
  constexpr std::size_t kStack = 64;
  Value stack[kStack];
  std::size_t top = 0;
  const auto pop = [&]() -> Value {
    if (top == 0) {
      return 0.0f;
    }
    return stack[--top];
  };
  const auto push = [&](Value v) {
    if (top < kStack) {
      stack[top++] = v;
    }
  };
  const auto lift = [](auto a_f) { return [a_f](float x) { return a_f(x); }; };

  for (const auto &node : code_) {
    switch (node.op) {
    case Op::kNumber:
      push(node.number);
      break;
    case Op::kMakeVec2: {
      const float y = AsScalar(pop()), x = AsScalar(pop());
      push(Vec2{x, y});
      break;
    }
    case Op::kMakeVec3: {
      const float z = AsScalar(pop()), y = AsScalar(pop()), x = AsScalar(pop());
      push(Vec3{x, y, z});
      break;
    }
    case Op::kRef:
      push(node.index < a_inputs.refs.size() ? a_inputs.refs[node.index]
                                             : Value{0.0f});
      break;
    case Op::kCurve: {
      const float x = AsScalar(pop());
      const Program *curve = node.index < a_inputs.curves.size()
                                 ? a_inputs.curves[node.index]
                                 : nullptr;
      push(curve ? ApplyCurve(*curve, x, a_inputs.mean) : x);
      break;
    }
    case Op::kX:
      push(a_inputs.x);
      break;
    case Op::kMean:
      push(a_inputs.mean);
      break;
    case Op::kTime:
      push(a_inputs.time);
      break;
    case Op::kNeg:
      push(Unary(pop(), lift([](float x) { return -x; })));
      break;
    case Op::kNot:
      push(1.0f - Truth(pop()));
      break;
    case Op::kAdd: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) { return x + y; }));
      break;
    }
    case Op::kSub: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) { return x - y; }));
      break;
    }
    case Op::kMul: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) { return x * y; }));
      break;
    }
    case Op::kDiv: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) {
        return std::fabs(y) <= kEpsilon ? 0.0f : x / y;
      }));
      break;
    }
    case Op::kLt: {
      const auto b = pop(), a = pop();
      push(AsScalar(a) < AsScalar(b) ? 1.0f : 0.0f);
      break;
    }
    case Op::kGt: {
      const auto b = pop(), a = pop();
      push(AsScalar(a) > AsScalar(b) ? 1.0f : 0.0f);
      break;
    }
    case Op::kLe: {
      const auto b = pop(), a = pop();
      push(AsScalar(a) <= AsScalar(b) ? 1.0f : 0.0f);
      break;
    }
    case Op::kGe: {
      const auto b = pop(), a = pop();
      push(AsScalar(a) >= AsScalar(b) ? 1.0f : 0.0f);
      break;
    }
    case Op::kEq: {
      const auto b = pop(), a = pop();
      push(std::fabs(AsScalar(a) - AsScalar(b)) <= kEpsilon ? 1.0f : 0.0f);
      break;
    }
    case Op::kNe: {
      const auto b = pop(), a = pop();
      push(std::fabs(AsScalar(a) - AsScalar(b)) > kEpsilon ? 1.0f : 0.0f);
      break;
    }
    case Op::kAnd: {
      const auto b = pop(), a = pop();
      push(Truth(a) * Truth(b));
      break;
    }
    case Op::kOr: {
      const auto b = pop(), a = pop();
      push(std::max(Truth(a), Truth(b)));
      break;
    }
    case Op::kIf: {
      const auto b = pop(), a = pop(), c = pop();
      push(Truth(c) > 0.0f ? a : b);
      break;
    }
    case Op::kAbs:
      push(Unary(pop(), lift([](float x) { return std::fabs(x); })));
      break;
    case Op::kMin: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) { return std::min(x, y); }));
      break;
    }
    case Op::kMax: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) { return std::max(x, y); }));
      break;
    }
    case Op::kClamp: {
      const auto hi = pop(), lo = pop(), x = pop();
      push(Ternary(x, lo, hi, [](float v, float l, float h) {
        return std::clamp(v, std::min(l, h), std::max(l, h));
      }));
      break;
    }
    case Op::kSaturate:
      push(Unary(pop(), lift([](float x) { return Clamp01(x); })));
      break;
    case Op::kFloor:
      push(Unary(pop(), lift([](float x) { return std::floor(x); })));
      break;
    case Op::kCeil:
      push(Unary(pop(), lift([](float x) { return std::ceil(x); })));
      break;
    case Op::kFrac:
      push(Unary(pop(), lift([](float x) { return x - std::floor(x); })));
      break;
    case Op::kSqrt:
      push(Unary(pop(),
                 lift([](float x) { return std::sqrt(std::max(0.0f, x)); })));
      break;
    case Op::kPow: {
      const auto b = pop(), a = pop();
      push(Binary(a, b, [](float x, float y) {
        const float r = std::pow(x, y);
        return std::isfinite(r) ? r : 0.0f;
      }));
      break;
    }
    case Op::kSin:
      push(Unary(pop(), lift([](float x) { return std::sin(x); })));
      break;
    case Op::kCos:
      push(Unary(pop(), lift([](float x) { return std::cos(x); })));
      break;
    case Op::kStep: {
      const auto x = pop(), edge = pop();
      push(Binary(edge, x,
                  [](float e, float v) { return v < e ? 0.0f : 1.0f; }));
      break;
    }
    case Op::kSmoothstep: {
      const auto x = pop(), hi = pop(), lo = pop();
      push(Ternary(lo, hi, x, [](float l, float h, float v) {
        return Smoothstep(l, h, v);
      }));
      break;
    }
    case Op::kLerp: {
      const auto t = pop(), b = pop(), a = pop();
      push(Ternary(a, b, t,
                   [](float x, float y, float s) { return x + (y - x) * s; }));
      break;
    }
    }
  }
  return top > 0 ? stack[top - 1] : Value{0.0f};
}

float ApplyCurve(const Program &a_curve, float a_x, float a_mean) noexcept {
  Program::Inputs in;
  in.x = a_x;
  in.mean = a_mean;
  return AsScalar(a_curve.Evaluate(in));
}
}
