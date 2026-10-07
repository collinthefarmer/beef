// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Expression.h"

#include "recipe/ExpressionMath.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kEpsilon = 1e-6f;

constexpr std::array<ExpressionFunction, 27> kFunctions{{
    {"abs", "abs(x)", FixedCall{Program::Op::kAbs, 1}},
    {"min", "min(a, b)", FixedCall{Program::Op::kMin, 2}},
    {"max", "max(a, b)", FixedCall{Program::Op::kMax, 2}},
    {"clamp", "clamp(value, low, high)", FixedCall{Program::Op::kClamp, 3}},
    {"saturate", "saturate(x)", FixedCall{Program::Op::kSaturate, 1}},
    {"floor", "floor(x)", FixedCall{Program::Op::kFloor, 1}},
    {"ceil", "ceil(x)", FixedCall{Program::Op::kCeil, 1}},
    {"frac", "frac(x)", FixedCall{Program::Op::kFrac, 1}},
    {"sqrt", "sqrt(x)", FixedCall{Program::Op::kSqrt, 1}},
    {"pow", "pow(base, exponent)", FixedCall{Program::Op::kPow, 2}},
    {"sin", "sin(x)", FixedCall{Program::Op::kSin, 1}},
    {"cos", "cos(x)", FixedCall{Program::Op::kCos, 1}},
    {"step", "step(edge, x)", FixedCall{Program::Op::kStep, 2}},
    {"smoothstep", "smoothstep(low, high, x)",
     FixedCall{Program::Op::kSmoothstep, 3}},
    {"lerp", "lerp(a, b, t)", FixedCall{Program::Op::kLerp, 3}},
    {"if", "if(condition, then, else)", FixedCall{Program::Op::kIf, 3}},
    {"length", "length(v)", FixedCall{Program::Op::kLength, 1}},
    {"distance", "distance(a, b)", FixedCall{Program::Op::kDistance, 2}},
    {"dot", "dot(a, b)", FixedCall{Program::Op::kDot, 2}},
    {"cross", "cross(a, b)", FixedCall{Program::Op::kCross, 2}},
    {"normalize", "normalize(v)", FixedCall{Program::Op::kNormalize, 1}},
    {"shuffle", "shuffle(index, total, seed)",
     FixedCall{Program::Op::kShuffle, 3}},
    {"hash", "hash(value, salt)", FixedCall{Program::Op::kHash, 2}},
    {"rand", "rand([salt])", RandomCall{}},
    {"randrange", "randrange(start, stop[, seed])", RandomRangeCall{}},
    {"hue", "hue(turns)", FixedCall{Program::Op::kHue, 1}},
    {"hueShift", "hueShift(color, turns)",
     FixedCall{Program::Op::kHueShift, 2}},
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

float Length(const Value &a) noexcept {
  return Match(
      a, [](float x) { return std::fabs(x); },
      [](const Vec2 &v) { return std::sqrt(v.x * v.x + v.y * v.y); },
      [](const Vec3 &v) {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
      });
}

float Dot(const Value &a, const Value &b) noexcept {
  const auto ta = TypeOf(a), tb = TypeOf(b);
  if (ta != tb || ta == ValueType::kScalar) {
    return 0.0f;
  }
  const Vec3 x = AsVec3(a), y = AsVec3(b);
  return x.x * y.x + x.y * y.y + x.z * y.z;
}

Value Cross(const Value &a, const Value &b) noexcept {
  const auto *x = Get<Vec3>(a);
  const auto *y = Get<Vec3>(b);
  if (!x || !y) {
    return 0.0f;
  }
  return Vec3{x->y * y->z - x->z * y->y, x->z * y->x - x->x * y->z,
              x->x * y->y - x->y * y->x};
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

class TypeStack {
public:
  TypeStack() { values_.reserve(16); }
  void Push(ValueType type) { values_.push_back(type); }
  ValueType Pop() {
    if (values_.empty()) {
      return ValueType::kScalar;
    }
    const auto type = values_.back();
    values_.pop_back();
    return type;
  }
  bool TakeScalars(int count) {
    for (int i = 0; i < count; ++i) {
      if (Pop() != ValueType::kScalar) {
        return false;
      }
    }
    return true;
  }
  std::expected<ValueType, std::string> JoinOperands(int count,
                                                     std::string_view context) {
    std::array<ValueType, 3> operands{};
    for (int i = count; i > 0; --i) {
      operands[i - 1] = Pop();
    }
    auto type = operands[0];
    for (int i = 1; i < count; ++i) {
      auto joined = Join(type, operands[i], context);
      if (!joined) {
        return joined;
      }
      type = *joined;
    }
    return type;
  }

private:
  std::vector<ValueType> values_;
};

class ValueStack {
public:
  Value Pop() noexcept { return top_ == 0 ? Value{0.0f} : values_[--top_]; }
  void Push(Value value) noexcept {
    if (top_ < values_.size()) {
      values_[top_++] = value;
    }
  }
  template <class F> void ApplyUnary(F operation) noexcept {
    Push(Unary(Pop(), operation));
  }
  template <class F> void ApplyBinary(F operation) noexcept {
    const auto b = Pop(), a = Pop();
    Push(Binary(a, b, operation));
  }
  template <class F> void ApplyScalarBinary(F operation) noexcept {
    const auto b = Pop(), a = Pop();
    Push(operation(AsScalar(a), AsScalar(b)));
  }
  template <class F> void ApplyTernary(F operation) noexcept {
    const auto c = Pop(), b = Pop(), a = Pop();
    Push(Ternary(a, b, c, operation));
  }

private:
  std::array<Value, 64> values_;
  std::size_t top_ = 0;
};

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
      const std::size_t sign = pos_;
      ++pos_;
      Skip();
      const std::size_t operand = pos_;
      if (auto e = ParseUnary()) {
        return e;
      }
      if (!out_.literals_.empty()) {
        NumericLiteral &literal = out_.literals_.back();
        if (literal.offset == operand &&
            literal.offset + literal.length == pos_) {
          literal.offset = sign;
          literal.length = pos_ - sign;
          literal.value = -literal.value;
        }
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
    const std::size_t start = pos_;
    float value = 0.0f;
    const auto r = std::from_chars(text_.data() + pos_,
                                   text_.data() + text_.size(), value);
    if (r.ec != std::errc{} || !std::isfinite(value)) {
      return std::format("bad number at {}", pos_);
    }
    pos_ = IndexOf(text_, r.ptr);
    out_.literals_.push_back(NumericLiteral{start, pos_ - start, value});
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
    const auto fn =
        std::ranges::find(kFunctions, name, &ExpressionFunction::name);
    if (fn == kFunctions.end()) {
      return std::format("unknown name '{}' at {}; rows are written @{}", name,
                         pos_, name);
    }
    return Match(
        fn->form,
        [&](const FixedCall &a_call) { return ParseFixedCall(name, a_call); },
        [&](const RandomCall &) { return ParseRand(); },
        [&](const RandomRangeCall &) { return ParseRandRange(); });
  }

  Error ParseFixedCall(std::string_view a_name, const FixedCall &a_call) {
    if (!Take("(")) {
      return std::format("'{}' is a function and needs '('", a_name);
    }
    for (int i = 0; i < a_call.arity; ++i) {
      if (i > 0 && !Take(",")) {
        return std::format("'{}' takes {} arguments", a_name, a_call.arity);
      }
      if (auto e = ParseOr()) {
        return e;
      }
    }
    if (!Take(")")) {
      return std::format("'{}' takes {} arguments", a_name, a_call.arity);
    }
    return Emit(a_call.op);
  }

  Error EmitRand() {
    out_.usesIdentity_ = true;
    if (auto e = Emit(Op::kIdentity)) {
      return e;
    }
    if (auto e = Emit(Op::kNumber, static_cast<float>(randomCalls_++))) {
      return e;
    }
    return Emit(Op::kHash);
  }

  Error ParseRand() {
    constexpr std::string_view kShape =
        "'rand' takes an optional salt: rand() or rand(salt)";
    if (!Take("(")) {
      return std::string{kShape};
    }
    if (Take(")")) {
      return EmitRand();
    }
    out_.usesIdentity_ = true;
    if (auto e = Emit(Op::kIdentity)) {
      return e;
    }
    if (auto e = ParseOr()) {
      return e;
    }
    if (!Take(")")) {
      return std::string{kShape};
    }
    return Emit(Op::kHash);
  }

  Error ParseRandRange() {
    constexpr std::string_view kShape =
        "'randrange' takes a start, a stop and an optional seed";
    if (!Take("(")) {
      return std::string{kShape};
    }
    if (auto e = ParseOr()) {
      return e;
    }
    if (!Take(",")) {
      return std::string{kShape};
    }
    if (auto e = ParseOr()) {
      return e;
    }
    if (Take(",")) {
      if (auto e = ParseOr()) {
        return e;
      }
    } else if (auto e = EmitRand()) {
      return e;
    }
    if (!Take(")")) {
      return std::string{kShape};
    }
    return Emit(Op::kRandRange);
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t depth_ = 0;
  std::uint32_t randomCalls_ = 0;
  Program out_;
};

std::expected<Program, std::string> Program::Parse(std::string_view a_text) {
  return ExpressionParser{a_text}.Run();
}

std::expected<std::string, std::string>
ReplaceNumericLiteral(std::string_view a_current,
                      const NumericLiteralSelection &a_selection,
                      std::string_view a_replacement) {
  if (a_current != a_selection.expression) {
    return std::unexpected("the expression changed; select the operand again");
  }
  const auto original = Program::Parse(a_current);
  if (!original) {
    return std::unexpected(original.error());
  }
  const std::span<const NumericLiteral> literals = original->NumericLiterals();
  if (a_selection.index >= literals.size()) {
    return std::unexpected("the selected numeric operand does not exist");
  }
  const auto replacement = Program::Parse(a_replacement);
  if (!replacement) {
    return std::unexpected(replacement.error());
  }
  const bool atomic =
      replacement->Code().size() == 1 ||
      (replacement->NumericLiterals().size() == 1 &&
       std::ranges::all_of(replacement->Code(),
                           [](const Program::Node &a_node) {
                             return a_node.op == Program::Op::kNumber ||
                                    a_node.op == Program::Op::kNeg;
                           }));
  const std::size_t grouping = atomic ? 0 : 2;
  const NumericLiteral &literal = literals[a_selection.index];
  if (a_replacement.size() + grouping >
      kMaxExpressionLength - (a_current.size() - literal.length)) {
    return std::unexpected("the replacement makes the expression too long");
  }
  std::string changed{a_current.substr(0, literal.offset)};
  if (!atomic) {
    changed += '(';
  }
  changed += a_replacement;
  if (!atomic) {
    changed += ')';
  }
  changed += a_current.substr(literal.offset + literal.length);
  if (const auto parsed = Program::Parse(changed); !parsed) {
    return std::unexpected(parsed.error());
  }
  return changed;
}

std::span<const ExpressionFunction> ExpressionFunctions() noexcept {
  return kFunctions;
}

Program Program::Hash(std::string a_value, std::string a_salt) {
  Program program;
  program.refs_ = {std::move(a_value), std::move(a_salt)};
  program.code_ = {{Op::kRef, 0.0f, 0}, {Op::kRef, 0.0f, 1}, {Op::kHash}};
  return program;
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
  if (program->UsesIdentity()) {
    return std::unexpected(
        "a curve is a function of x and cannot use rand() or randrange() "
        "without a seed");
  }
  return program;
}

namespace {
struct ExpressionTyping {
  std::span<const std::string> references;
  std::span<const std::string> curves;
  const RefTyper &types;
  ValueType xType = ValueType::kScalar;
};

std::expected<ValueType, std::string> CheckVectorFunction(Program::Op a_op,
                                                          TypeStack &a_stack) {
  using Op = Program::Op;
  switch (a_op) {
  case Op::kLength:
    if (a_stack.Pop() == ValueType::kScalar) {
      return std::unexpected("length() takes a vector");
    }
    return ValueType::kScalar;
  case Op::kDistance: {
    const auto b = a_stack.Pop(), a = a_stack.Pop();
    if (a == ValueType::kScalar || a != b) {
      return std::unexpected("distance() takes two vectors of the same size");
    }
    return ValueType::kScalar;
  }
  case Op::kDot: {
    const auto b = a_stack.Pop(), a = a_stack.Pop();
    if (a == ValueType::kScalar || a != b) {
      return std::unexpected("dot() takes two vectors of the same size");
    }
    return ValueType::kScalar;
  }
  case Op::kCross: {
    const auto b = a_stack.Pop(), a = a_stack.Pop();
    if (a != ValueType::kVec3 || b != ValueType::kVec3) {
      return std::unexpected("cross() takes two vec3s");
    }
    return ValueType::kVec3;
  }
  case Op::kNormalize: {
    const auto t = a_stack.Pop();
    if (t == ValueType::kScalar) {
      return std::unexpected("normalize() takes a vector");
    }
    return t;
  }
  default:
    return std::unexpected("unknown vector function");
  }
}

std::expected<ValueType, std::string> CheckIf(TypeStack &a_stack) {
  const auto b = a_stack.Pop(), a = a_stack.Pop(), c = a_stack.Pop();
  if (c != ValueType::kScalar) {
    return std::unexpected("if() takes a scalar condition");
  }
  return Join(a, b, "if");
}

std::expected<ValueType, std::string>
CheckReference(std::uint32_t a_index, const ExpressionTyping &a_typing) {
  if (a_index >= a_typing.references.size()) {
    return std::unexpected("unknown row");
  }
  const auto &name = a_typing.references[a_index];
  const auto t = a_typing.types(name);
  if (!t) {
    return std::unexpected(std::format("unknown row '@{}'", name));
  }
  return *t;
}

std::expected<ValueType, std::string>
CheckCurveCall(std::uint32_t a_index, TypeStack &a_stack,
               const ExpressionTyping &a_typing) {
  if (a_stack.Pop() != ValueType::kScalar) {
    const std::string_view name =
        a_index < a_typing.curves.size()
            ? std::string_view{a_typing.curves[a_index]}
            : std::string_view{};
    return std::unexpected(std::format("curve '@{}' takes a scalar", name));
  }
  return ValueType::kScalar;
}

std::expected<ValueType, std::string>
CheckNode(const Program::Node &a_node, TypeStack &a_stack,
          const ExpressionTyping &a_typing) {
  using Op = Program::Op;
  switch (a_node.op) {
  case Op::kNumber:
  case Op::kTime:
  case Op::kMean:
  case Op::kIdentity:
    return ValueType::kScalar;
  case Op::kX:
    return a_typing.xType;
  case Op::kMakeVec2:
    if (!a_stack.TakeScalars(2)) {
      return std::unexpected("vector components must be scalars");
    }
    return ValueType::kVec2;
  case Op::kMakeVec3:
    if (!a_stack.TakeScalars(3)) {
      return std::unexpected("vector components must be scalars");
    }
    return ValueType::kVec3;
  case Op::kRef:
    return CheckReference(a_node.index, a_typing);
  case Op::kCurve:
    return CheckCurveCall(a_node.index, a_stack, a_typing);
  case Op::kNeg:
  case Op::kAbs:
  case Op::kSaturate:
  case Op::kFloor:
  case Op::kCeil:
  case Op::kFrac:
  case Op::kSqrt:
  case Op::kSin:
  case Op::kCos:
    return a_stack.Pop();
  case Op::kNot:
    if (!a_stack.TakeScalars(1)) {
      return std::unexpected("comparisons and logic take scalars");
    }
    return ValueType::kScalar;
  case Op::kLt:
  case Op::kGt:
  case Op::kLe:
  case Op::kGe:
  case Op::kEq:
  case Op::kNe:
  case Op::kAnd:
  case Op::kOr:
    if (!a_stack.TakeScalars(2)) {
      return std::unexpected("comparisons and logic take scalars");
    }
    return ValueType::kScalar;
  case Op::kShuffle:
    if (!a_stack.TakeScalars(3)) {
      return std::unexpected("shuffle() takes three scalars");
    }
    return ValueType::kScalar;
  case Op::kHash:
    if (!a_stack.TakeScalars(2)) {
      return std::unexpected("hash() takes two scalars");
    }
    return ValueType::kScalar;
  case Op::kRandRange:
    if (!a_stack.TakeScalars(3)) {
      return std::unexpected("randrange() takes scalars");
    }
    return ValueType::kScalar;
  case Op::kHue:
    if (!a_stack.TakeScalars(1)) {
      return std::unexpected("hue() takes a scalar number of turns");
    }
    return ValueType::kVec3;
  case Op::kHueShift: {
    const auto turns = a_stack.Pop(), color = a_stack.Pop();
    if (color != ValueType::kVec3 || turns != ValueType::kScalar) {
      return std::unexpected(
          "hueShift() takes a colour and a scalar number of turns");
    }
    return ValueType::kVec3;
  }
  case Op::kAdd:
  case Op::kSub:
  case Op::kMul:
  case Op::kDiv:
  case Op::kMin:
  case Op::kMax:
  case Op::kPow:
  case Op::kStep:
    return a_stack.JoinOperands(2, "operator");
  case Op::kClamp:
  case Op::kSmoothstep:
  case Op::kLerp:
    return a_stack.JoinOperands(3, "function");
  case Op::kIf:
    return CheckIf(a_stack);
  case Op::kLength:
  case Op::kDistance:
  case Op::kDot:
  case Op::kCross:
  case Op::kNormalize:
    return CheckVectorFunction(a_node.op, a_stack);
  }
  return std::unexpected("unknown operation");
}
}

std::expected<ValueType, std::string> Program::Check(const RefTyper &a_types,
                                                     ValueType a_xType) const {
  const ExpressionTyping typing{refs_, curves_, a_types, a_xType};
  TypeStack stack;
  for (const Node &node : code_) {
    const std::expected<ValueType, std::string> type =
        CheckNode(node, stack, typing);
    if (!type) {
      return std::unexpected(type.error());
    }
    stack.Push(*type);
  }
  return stack.Pop();
}

Value Program::Evaluate(const Inputs &a_inputs) const noexcept {
  ValueStack stack;

  for (const auto &node : code_) {
    switch (node.op) {
    case Op::kNumber:
      stack.Push(node.number);
      break;
    case Op::kMakeVec2: {
      const float y = AsScalar(stack.Pop()), x = AsScalar(stack.Pop());
      stack.Push(Vec2{x, y});
      break;
    }
    case Op::kMakeVec3: {
      const float z = AsScalar(stack.Pop()), y = AsScalar(stack.Pop()),
                  x = AsScalar(stack.Pop());
      stack.Push(Vec3{x, y, z});
      break;
    }
    case Op::kRef:
      stack.Push(node.index < a_inputs.refs.size() ? a_inputs.refs[node.index]
                                                   : Value{0.0f});
      break;
    case Op::kCurve: {
      const float x = AsScalar(stack.Pop());
      const Program *curve = node.index < a_inputs.curves.size()
                                 ? a_inputs.curves[node.index]
                                 : nullptr;
      stack.Push(a_inputs.callFunction
                     ? a_inputs.callFunction(node.index, x, a_inputs.mean)
                 : curve ? ApplyCurve(*curve, x, a_inputs.mean)
                         : x);
      break;
    }
    case Op::kX:
      stack.Push(a_inputs.x);
      break;
    case Op::kMean:
      stack.Push(a_inputs.mean);
      break;
    case Op::kTime:
      stack.Push(a_inputs.time);
      break;
    case Op::kIdentity:
      stack.Push(a_inputs.identity);
      break;
    case Op::kHash:
      stack.ApplyScalarBinary([](float a, float b) { return HashUnit(a, b); });
      break;
    case Op::kHue:
      stack.Push(HueColor(AsVec3(stack.Pop()).x));
      break;
    case Op::kHueShift: {
      const auto turns = stack.Pop(), color = stack.Pop();
      stack.Push(ShiftHue(AsVec3(color), AsVec3(turns).x));
      break;
    }
    case Op::kRandRange: {
      const auto seed = stack.Pop(), stop = stack.Pop(), start = stack.Pop();
      stack.Push(
          RandomInRange(AsVec3(start).x, AsVec3(stop).x, AsVec3(seed).x));
      break;
    }
    case Op::kNeg:
      stack.ApplyUnary([](float x) { return -x; });
      break;
    case Op::kNot:
      stack.Push(1.0f - Truth(stack.Pop()));
      break;
    case Op::kAdd:
      stack.ApplyBinary([](float x, float y) { return x + y; });
      break;
    case Op::kSub:
      stack.ApplyBinary([](float x, float y) { return x - y; });
      break;
    case Op::kMul:
      stack.ApplyBinary([](float x, float y) { return x * y; });
      break;
    case Op::kDiv:
      stack.ApplyBinary([](float x, float y) {
        return std::fabs(y) <= kEpsilon ? 0.0f : x / y;
      });
      break;
    case Op::kLt:
      stack.ApplyScalarBinary(
          [](float a, float b) { return a < b ? 1.0f : 0.0f; });
      break;
    case Op::kGt:
      stack.ApplyScalarBinary(
          [](float a, float b) { return a > b ? 1.0f : 0.0f; });
      break;
    case Op::kLe:
      stack.ApplyScalarBinary(
          [](float a, float b) { return a <= b ? 1.0f : 0.0f; });
      break;
    case Op::kGe:
      stack.ApplyScalarBinary(
          [](float a, float b) { return a >= b ? 1.0f : 0.0f; });
      break;
    case Op::kEq:
      stack.ApplyScalarBinary([](float a, float b) {
        return std::fabs(a - b) <= kEpsilon ? 1.0f : 0.0f;
      });
      break;
    case Op::kNe:
      stack.ApplyScalarBinary([](float a, float b) {
        return std::fabs(a - b) > kEpsilon ? 1.0f : 0.0f;
      });
      break;
    case Op::kAnd:
      stack.ApplyScalarBinary(
          [](float a, float b) { return Truth(a) * Truth(b); });
      break;
    case Op::kOr:
      stack.ApplyScalarBinary(
          [](float a, float b) { return std::max(Truth(a), Truth(b)); });
      break;
    case Op::kIf: {
      const auto b = stack.Pop(), a = stack.Pop(), c = stack.Pop();
      stack.Push(Truth(c) > 0.0f ? a : b);
      break;
    }
    case Op::kAbs:
      stack.ApplyUnary([](float x) { return std::fabs(x); });
      break;
    case Op::kMin:
      stack.ApplyBinary([](float x, float y) { return std::min(x, y); });
      break;
    case Op::kMax:
      stack.ApplyBinary([](float x, float y) { return std::max(x, y); });
      break;
    case Op::kClamp:
      stack.ApplyTernary([](float v, float l, float h) {
        return std::clamp(v, std::min(l, h), std::max(l, h));
      });
      break;
    case Op::kSaturate:
      stack.ApplyUnary([](float x) { return Clamp01(x); });
      break;
    case Op::kFloor:
      stack.ApplyUnary([](float x) { return std::floor(x); });
      break;
    case Op::kCeil:
      stack.ApplyUnary([](float x) { return std::ceil(x); });
      break;
    case Op::kFrac:
      stack.ApplyUnary([](float x) { return x - std::floor(x); });
      break;
    case Op::kSqrt:
      stack.ApplyUnary([](float x) { return std::sqrt(std::max(0.0f, x)); });
      break;
    case Op::kPow:
      stack.ApplyBinary([](float x, float y) {
        const float r = std::pow(x, y);
        return std::isfinite(r) ? r : 0.0f;
      });
      break;
    case Op::kSin:
      stack.ApplyUnary([](float x) { return std::sin(x); });
      break;
    case Op::kCos:
      stack.ApplyUnary([](float x) { return std::cos(x); });
      break;
    case Op::kStep:
      stack.ApplyBinary([](float e, float v) { return v < e ? 0.0f : 1.0f; });
      break;
    case Op::kSmoothstep:
      stack.ApplyTernary(
          [](float l, float h, float v) { return Smoothstep(l, h, v); });
      break;
    case Op::kLerp:
      stack.ApplyTernary(
          [](float x, float y, float s) { return x + (y - x) * s; });
      break;
    case Op::kShuffle: {
      const auto seed = stack.Pop(), total = stack.Pop(), index = stack.Pop();
      stack.Push(
          ShufflePosition(AsVec3(index).x, AsVec3(total).x, AsVec3(seed).x));
      break;
    }
    case Op::kLength:
      stack.Push(Length(stack.Pop()));
      break;
    case Op::kDistance: {
      const auto b = stack.Pop(), a = stack.Pop();
      stack.Push(Length(Binary(a, b, [](float x, float y) { return x - y; })));
      break;
    }
    case Op::kDot: {
      const auto b = stack.Pop(), a = stack.Pop();
      stack.Push(Dot(a, b));
      break;
    }
    case Op::kCross: {
      const auto b = stack.Pop(), a = stack.Pop();
      stack.Push(Cross(a, b));
      break;
    }
    case Op::kNormalize: {
      const auto a = stack.Pop();
      const float len = Length(a);
      stack.Push(Unary(
          a, [len](float x) { return len <= kEpsilon ? 0.0f : x / len; }));
      break;
    }
    }
  }
  return stack.Pop();
}

float ApplyCurve(const Program &a_curve, float a_x, float a_mean) noexcept {
  Program::Inputs in;
  in.x = a_x;
  in.mean = a_mean;
  return AsScalar(a_curve.Evaluate(in));
}

std::string RenameInExpression(std::string_view a_text,
                               std::span<const ExpressionRename> a_renames,
                               bool a_curve) {
  const auto nameChar = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  };
  std::string out;
  out.reserve(a_text.size());
  std::size_t at = 0;
  while (at < a_text.size()) {
    if (a_text[at] != '@') {
      out += a_text[at++];
      continue;
    }
    std::size_t end = at + 1;
    while (end < a_text.size() && nameChar(a_text[end])) {
      ++end;
    }
    const std::string_view name = a_text.substr(at + 1, end - at - 1);
    std::size_t next = end;
    while (next < a_text.size() &&
           std::isspace(static_cast<unsigned char>(a_text[next]))) {
      ++next;
    }
    const bool call = next < a_text.size() && a_text[next] == '(';
    if (!name.empty() && call == a_curve) {
      const ExpressionRename *rename =
          FindBy(a_renames, name, &ExpressionRename::from);
      if (rename) {
        out += '@';
        out += rename->to;
        at = end;
        continue;
      }
    }
    out += a_text[at];
    ++at;
  }
  return out;
}

std::string RenameInExpression(std::string_view a_text, std::string_view a_from,
                               std::string_view a_to, bool a_curve) {
  const ExpressionRename rename{std::string{a_from}, std::string{a_to}};
  return RenameInExpression(a_text, std::span{&rename, 1}, a_curve);
}

std::string ExpressionSummary(std::string_view a_text, std::size_t a_max) {
  std::string collapsed;
  bool pendingSpace = false;
  for (const char c : a_text) {
    if (static_cast<bool>(std::isspace(static_cast<unsigned char>(c)))) {
      pendingSpace = !collapsed.empty();
      continue;
    }
    if (pendingSpace) {
      collapsed.push_back(' ');
      pendingSpace = false;
    }
    collapsed.push_back(c);
  }
  if (a_max < 3 || collapsed.size() <= a_max) {
    return collapsed;
  }
  const std::size_t budget = a_max - 1;
  std::size_t head = (budget + 1) / 2;
  std::size_t tailStart = collapsed.size() - (budget - head);
  if (const std::size_t space = collapsed.rfind(' ', head);
      space != std::string::npos && space > 0) {
    head = space;
  }
  if (const std::size_t space = collapsed.find(' ', tailStart);
      space != std::string::npos && space + 1 < collapsed.size()) {
    tailStart = space + 1;
  }
  return collapsed.substr(0, head) + "…" + collapsed.substr(tailStart);
}
}

namespace BetterEnchantmentEffects {
Program Program::BindContext(std::optional<std::uint32_t> time,
                             std::optional<std::uint32_t> x,
                             std::optional<std::uint32_t> mean,
                             std::optional<std::uint32_t> identity) const {
  Program bound = *this;
  for (auto &op : bound.code_) {
    const auto binding = op.op == Op::kTime       ? time
                         : op.op == Op::kX        ? x
                         : op.op == Op::kMean     ? mean
                         : op.op == Op::kIdentity ? identity
                                                  : std::nullopt;
    if (!binding)
      continue;
    op.op = Op::kRef;
    op.index = *binding;
    if (bound.refs_.size() <= *binding)
      bound.refs_.resize(static_cast<std::size_t>(*binding) + 1);
  }
  if (time)
    bound.usesTime_ = false;
  if (x)
    bound.usesX_ = false;
  if (mean)
    bound.usesMean_ = false;
  if (identity)
    bound.usesIdentity_ = false;
  return bound;
}
}
