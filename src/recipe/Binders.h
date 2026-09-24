// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
using json = nlohmann::ordered_json;

[[nodiscard]] bool RowCapReached(std::size_t a_count, const Reporter &a_ctx,
                                 std::string_view a_what,
                                 std::size_t a_cap = kMaxRecipeRows);
[[nodiscard]] std::size_t MaxNestingDepth(std::string_view a_json) noexcept;
[[nodiscard]] std::optional<json> ParseObjectDocument(std::string_view a_json,
                                                      const Reporter &a_ctx);

[[nodiscard]] std::optional<float>
FloatFrom(const json &a_value, std::string_view a_what, const Reporter &a_ctx);

class Reader {
public:
  Reader(const json &a_object, Reporter a_ctx)
      : object_(a_object), ctx_(std::move(a_ctx)) {}

  [[nodiscard]] const Reporter &Context() const noexcept { return ctx_; }
  [[nodiscard]] const json &Object() const noexcept { return object_; }
  [[nodiscard]] bool Has(std::string_view a_key) const {
    return object_.is_object() && object_.contains(a_key);
  }

  const json *Child(std::string_view a_key) {
    if (!Has(a_key)) {
      return nullptr;
    }
    used_.insert(std::string{a_key});
    return &object_.at(a_key);
  }

  std::optional<float> Number(std::string_view a_key) {
    const auto *j = Child(a_key);
    if (!j) {
      return std::nullopt;
    }
    return FloatFrom(*j, a_key, ctx_);
  }

  std::optional<int> Integer(std::string_view a_key) {
    const auto *j = Child(a_key);
    if (!j) {
      return std::nullopt;
    }
    if (!j->is_number_integer()) {
      ctx_.Error(std::format("'{}' must be an integer", a_key));
      return std::nullopt;
    }
    const bool outOfRange =
        j->is_number_unsigned()
            ? j->get<std::uint64_t>() >
                  static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            : j->get<std::int64_t>() < std::numeric_limits<int>::min() ||
                  j->get<std::int64_t>() > std::numeric_limits<int>::max();
    if (outOfRange) {
      ctx_.Error(std::format("'{}' must be an integer in {}..{}", a_key,
                             std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max()));
      return std::nullopt;
    }
    return j->get<int>();
  }

  std::optional<bool> Boolean(std::string_view a_key) {
    const auto *j = Child(a_key);
    if (!j) {
      return std::nullopt;
    }
    if (!j->is_boolean()) {
      ctx_.Error(std::format("'{}' must be true or false", a_key));
      return std::nullopt;
    }
    return j->get<bool>();
  }

  std::optional<std::string> String(std::string_view a_key) {
    const auto *j = Child(a_key);
    if (!j) {
      return std::nullopt;
    }
    if (!j->is_string()) {
      ctx_.Error(std::format("'{}' must be a string", a_key));
      return std::nullopt;
    }
    return j->get<std::string>();
  }

  std::string Required(std::string_view a_key) {
    if (!Has(a_key)) {
      ctx_.Error(std::format("'{}' is required", a_key));
      return {};
    }
    return String(a_key).value_or(std::string{});
  }

  std::optional<std::vector<std::string>> Strings(std::string_view a_key,
                                                  std::size_t a_cap) {
    const auto *j = Child(a_key);
    if (!j) {
      return std::nullopt;
    }
    if (!j->is_array()) {
      ctx_.Error(std::format("'{}' must be an array of strings", a_key));
      return std::nullopt;
    }
    if (j->size() > a_cap) {
      ctx_.Error(std::format("'{}' has more than {} entries", a_key, a_cap));
      return std::nullopt;
    }
    std::vector<std::string> out;
    for (const json &element : *j) {
      if (!element.is_string()) {
        ctx_.Error(std::format("'{}' entries must be strings", a_key));
        return std::nullopt;
      }
      out.push_back(element.get<std::string>());
    }
    return out;
  }

  std::optional<BetterEnchantmentEffects::BipedSlot>
  BipedSlot(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? BipedSlotFrom(*j, a_key, ctx_) : std::nullopt;
  }

  template <class Row, std::size_t N>
  std::optional<decltype(Row::value)> Enum(std::string_view a_key,
                                           const Row (&a_table)[N]) {
    const auto text = String(a_key);
    if (!text) {
      return std::nullopt;
    }
    const auto value = FromName(a_table, *text);
    if (!value) {
      ctx_.Error(std::format("'{}' is not one of {}: '{}'", a_key,
                             Choices(a_table), *text));
    }
    return value;
  }

  std::optional<Ref> Reference(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? RefFrom(*j, a_key) : std::nullopt;
  }

  std::optional<Param> Parameter(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? ParamFrom(*j, a_key) : std::nullopt;
  }

  std::optional<Vec2Param> Vector2(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? VecFrom<2>(*j, a_key, false) : std::nullopt;
  }

  std::optional<Vec3Param> Vector3(std::string_view a_key,
                                   bool a_color = false) {
    const auto *j = Child(a_key);
    return j ? VecFrom<3>(*j, a_key, a_color) : std::nullopt;
  }

  static std::optional<Vec3> PointFrom(const json &a_j, std::string_view a_what,
                                       const Reporter &a_ctx) {
    if (!a_j.is_array() || a_j.size() != 3 ||
        !std::ranges::all_of(a_j,
                             [](const json &e) { return e.is_number(); })) {
      a_ctx.Error(std::format("'{}' must be [x, y, z]", a_what));
      return std::nullopt;
    }
    std::array<float, 3> parts{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto value = FloatFrom(a_j[i], a_what, a_ctx);
      if (!value) {
        return std::nullopt;
      }
      parts[i] = *value;
    }
    return Vec3{parts[0], parts[1], parts[2]};
  }

  std::optional<Vec3> Point(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? PointFrom(*j, a_key, ctx_) : std::nullopt;
  }

  std::optional<Value> Literal(std::string_view a_key) {
    const auto *j = Child(a_key);
    return j ? ValueFrom(*j, a_key, ctx_) : std::nullopt;
  }

  void Read(std::string_view a_key, Param &a_out) {
    if (auto p = Parameter(a_key))
      a_out = *p;
  }
  void Read(std::string_view a_key, std::optional<Param> &a_out) {
    if (auto p = Parameter(a_key))
      a_out = *p;
  }
  void Read(std::string_view a_key, bool &a_out) {
    if (auto b = Boolean(a_key))
      a_out = *b;
  }
  void Read(std::string_view a_key, Ref &a_out) {
    if (auto r = Reference(a_key))
      a_out = *r;
  }
  void Read(std::string_view a_key, std::optional<Ref> &a_out) {
    if (auto r = Reference(a_key))
      a_out = *r;
  }
  void Read(std::string_view a_key, Vec2Param &a_out) {
    if (auto v = Vector2(a_key))
      a_out = *v;
  }
  void Read(std::string_view a_key, std::optional<Vec2Param> &a_out) {
    if (auto v = Vector2(a_key))
      a_out = *v;
  }
  void Read(std::string_view a_key, Vec3Param &a_out, bool a_color = false) {
    if (auto v = Vector3(a_key, a_color))
      a_out = *v;
  }
  void Read(std::string_view a_key, std::optional<Vec3Param> &a_out,
            bool a_color = false) {
    if (auto v = Vector3(a_key, a_color))
      a_out = *v;
  }
  template <class Row, std::size_t N>
  void Read(std::string_view a_key, const Row (&a_table)[N],
            decltype(Row::value) &a_out) {
    if (auto e = Enum(a_key, a_table))
      a_out = *e;
  }

  template <class T>
  bool IntRange(std::string_view a_key, int a_lo, int a_hi, T &a_out) {
    const auto n = Integer(a_key);
    if (!n) {
      return true;
    }
    if (*n < a_lo || *n > a_hi) {
      ctx_.Error(std::format("'{}' is {}..{}", a_key, a_lo, a_hi));
      return false;
    }
    a_out = static_cast<T>(*n);
    return true;
  }

  void Finish() {
    if (!object_.is_object()) {
      return;
    }
    for (const auto &[key, value] : object_.items()) {
      if (!used_.contains(key)) {
        ctx_.Error(std::format("unknown key '{}'", key));
      }
    }
  }

  [[nodiscard]] std::optional<Ref> RefFrom(const json &a_j,
                                           std::string_view a_what) const {
    if (a_j.is_string()) {
      const auto text = a_j.get<std::string>();
      if (text.size() > 1 && text[0] == '@' && IsName(text.substr(1))) {
        return Ref{text.substr(1)};
      }
      ctx_.Error(std::format("'{}' names a row and must start with '@': '{}'",
                             a_what, text));
      return std::nullopt;
    }
    ctx_.Error(std::format("'{}' must be \"@name\"", a_what));
    return std::nullopt;
  }

  [[nodiscard]] std::optional<Param> ParamFrom(const json &a_j,
                                               std::string_view a_what) const {
    if (a_j.is_number()) {
      const auto value = FloatFrom(a_j, a_what, ctx_);
      return value ? std::optional<Param>{*value} : std::nullopt;
    }
    if (a_j.is_string()) {
      const auto ref = RefFrom(a_j, a_what);
      return ref ? std::optional<Param>{*ref} : std::nullopt;
    }
    ctx_.Error(std::format("'{}' must be a number or \"@name\"", a_what));
    return std::nullopt;
  }

  template <std::size_t N>
  [[nodiscard]] std::optional<std::variant<std::array<Param, N>, Ref>>
  VecFrom(const json &a_j, std::string_view a_what, bool a_color) const {
    using V = std::variant<std::array<Param, N>, Ref>;
    if (a_j.is_string()) {
      const auto ref = RefFrom(a_j, a_what);
      return ref ? std::optional<V>{*ref} : std::nullopt;
    }
    if (!a_j.is_array() || a_j.size() != N) {
      ctx_.Error(std::format(
          "'{}' must be an array of {} numbers or \"@name\"s, or one \"@name\"",
          a_what, N));
      return std::nullopt;
    }
    std::array<Param, N> parts;
    for (std::size_t i = 0; i < N; ++i) {
      const auto p = ParamFrom(a_j[i], a_what);
      if (!p) {
        return std::nullopt;
      }
      parts[i] = *p;
    }
    if constexpr (N == 3) {
      if (a_color) {
        NormaliseColor(parts);
      }
    }
    return V{parts};
  }

  static std::optional<Value>
  ValueFrom(const json &a_j, std::string_view a_what, const Reporter &a_ctx) {
    if (a_j.is_number()) {
      const auto value = FloatFrom(a_j, a_what, a_ctx);
      return value ? std::optional<Value>{*value} : std::nullopt;
    }
    if (a_j.is_array() && (a_j.size() == 2 || a_j.size() == 3) &&
        std::ranges::all_of(a_j, [](const json &e) { return e.is_number(); })) {
      std::array<float, 3> parts{};
      for (std::size_t i = 0; i < a_j.size(); ++i) {
        const auto value = FloatFrom(a_j[i], a_what, a_ctx);
        if (!value) {
          return std::nullopt;
        }
        parts[i] = *value;
      }
      if (a_j.size() == 2) {
        return Value{Vec2{parts[0], parts[1]}};
      }
      return Value{Vec3{parts[0], parts[1], parts[2]}};
    }
    a_ctx.Error(
        std::format("'{}' must be a number, [x, y] or [r, g, b]", a_what));
    return std::nullopt;
  }

  static std::optional<BetterEnchantmentEffects::BipedSlot>
  BipedSlotFrom(const json &a_j, std::string_view a_what,
                const Reporter &a_ctx) {
    if (a_j.is_string()) {
      const auto slot = BipedSlotFromName(a_j.get<std::string>());
      if (!slot) {
        a_ctx.Error(std::format("unknown biped slot name '{}'",
                                a_j.get<std::string>()));
      }
      return slot;
    }
    if (a_j.is_number_integer() &&
        a_j.get<double>() >= static_cast<int>(kFirstBipedSlot) &&
        a_j.get<double>() <= static_cast<int>(kLastBipedSlot)) {
      return BetterEnchantmentEffects::BipedSlot{a_j.get<std::uint32_t>()};
    }
    a_ctx.Error(std::format("'{}' is a biped slot name or a number {}..{}",
                            a_what, std::to_underlying(kFirstBipedSlot),
                            std::to_underlying(kLastBipedSlot)));
    return std::nullopt;
  }

private:
  const json &object_;
  Reporter ctx_;
  std::unordered_set<std::string> used_;
};

template <class Fill>
bool ReadObject(const json &a_v, std::string_view a_word, const Reporter &a_ctx,
                Fill a_fill) {
  if (!a_v.is_object()) {
    a_ctx.Error(std::format("'{}' takes an object", a_word));
    return false;
  }
  Reader inner(a_v, a_ctx);
  a_fill(inner);
  inner.Finish();
  return true;
}

template <class Row, std::size_t N>
std::optional<decltype(Row::value)>
EnumShorthand(const json &a_v, const Row (&a_table)[N], std::string_view a_what,
              const Reporter &a_ctx) {
  const auto value = a_v.is_string() ? FromName(a_table, a_v.get<std::string>())
                                     : std::nullopt;
  if (!value) {
    a_ctx.Error(std::format("'{}' is one of {}", a_what, Choices(a_table)));
  }
  return value;
}

template <class T, class Parse>
void ReadRows(const json &a_array, const char *a_word, const Reporter &a_ctx,
              std::vector<T> &a_out, Parse a_parse,
              std::size_t a_cap = kMaxRecipeRows) {
  std::size_t index = 0;
  for (const auto &element : a_array) {
    if (RowCapReached(index, a_ctx, a_word, a_cap)) {
      break;
    }
    if (auto row = a_parse(element, index)) {
      a_out.push_back(std::move(*row));
    }
    ++index;
  }
}

struct KindEntry {
  std::string key;
  const json *value = nullptr;
};

[[nodiscard]] std::optional<KindEntry>
OneKey(const json &a_j, const Reporter &a_ctx, std::string_view a_what,
       std::initializer_list<std::string_view> a_common = {});

template <class Row, class Where, class Parse>
void NamedRows(Reader &a_root, const char *a_section, Where a_where,
               std::vector<Row> &a_out, Parse a_parse) {
  const auto *section = a_root.Child(a_section);
  if (!section) {
    return;
  }
  if (!section->is_object()) {
    a_root.Context().Error(
        std::format("'{}' must be an object keyed by name", a_section));
    return;
  }
  std::size_t visited = 0;
  for (const auto &[name, value] : section->items()) {
    if (RowCapReached(visited++, a_root.Context(), a_section)) {
      break;
    }
    if (auto row = a_parse(name, value, a_root.Context().At(a_where(name)))) {
      a_out.push_back(std::move(*row));
    }
  }
}

[[nodiscard]] std::optional<SourceKind> ParseSourceKind(Reader &a_reader);

[[nodiscard]] json Num(float a_value);
[[nodiscard]] json ParamToJson(const Param &a_param);
[[nodiscard]] json ValueToJson(const Value &a_value);
[[nodiscard]] json PointToJson(const Vec3 &a_v);
[[nodiscard]] json BipedSlotToJson(BipedSlot a_slot);
[[nodiscard]] json SourceKindToJson(const SourceKind &a_kind);

template <std::size_t N>
json VecToJson(const std::variant<std::array<Param, N>, Ref> &a_param) {
  return Match(
      a_param, [](const Ref &r) { return json("@" + r.name); },
      [](const std::array<Param, N> &parts) {
        json out = json::array();
        for (const auto &p : parts) {
          out.push_back(ParamToJson(p));
        }
        return out;
      });
}

struct Writer {
  json &out;

  void Set(std::string_view a_key, json a_value) {
    out[std::string{a_key}] = std::move(a_value);
  }
  void Write(std::string_view a_key, const Param &a_value) {
    Set(a_key, ParamToJson(a_value));
  }
  void Write(std::string_view a_key, const Vec3Param &a_value) {
    Set(a_key, VecToJson(a_value));
  }
  void WriteText(std::string_view a_key, std::string_view a_value) {
    Set(a_key, std::string{a_value});
  }
  void WriteTextIf(std::string_view a_key, std::string_view a_value) {
    if (!a_value.empty())
      WriteText(a_key, a_value);
  }
  void WriteRef(std::string_view a_key, const Ref &a_ref) {
    Set(a_key, "@" + a_ref.name);
  }
  void WriteRefIf(std::string_view a_key, const std::optional<Ref> &a_ref) {
    if (a_ref)
      WriteRef(a_key, *a_ref);
  }

  void WriteIf(std::string_view a_key, const Param &a_value,
               const Param &a_default) {
    if (a_value != a_default)
      Write(a_key, a_value);
  }
  void WriteIf(std::string_view a_key, const Vec3Param &a_value,
               const Vec3Param &a_default) {
    if (a_value != a_default)
      Write(a_key, a_value);
  }
  void WriteIf(std::string_view a_key, bool a_value, bool a_default) {
    if (a_value != a_default)
      Set(a_key, a_value);
  }
  void WriteIf(std::string_view a_key, std::uint32_t a_value,
               std::uint32_t a_default) {
    if (a_value != a_default)
      Set(a_key, a_value);
  }
  void WriteIf(std::string_view a_key, const std::optional<Param> &a_value) {
    if (a_value)
      Write(a_key, *a_value);
  }
  void WriteIf(std::string_view a_key,
               const std::optional<Vec2Param> &a_value) {
    if (a_value)
      Set(a_key, VecToJson(*a_value));
  }
  void WriteIf(std::string_view a_key,
               const std::optional<Vec3Param> &a_value) {
    if (a_value)
      Set(a_key, VecToJson(*a_value));
  }
  void WriteNumberIf(std::string_view a_key, float a_value, float a_default) {
    if (a_value != a_default)
      Set(a_key, Num(a_value));
  }
  void WritePointIf(std::string_view a_key, const Vec3 &a_value,
                    const Vec3 &a_default) {
    if (!(a_value == a_default))
      Set(a_key, PointToJson(a_value));
  }
  void WriteStringsIf(std::string_view a_key,
                      const std::vector<std::string> &a_values) {
    if (!a_values.empty())
      Set(a_key, a_values);
  }

  template <class Row, std::size_t N, class E>
  void WriteEnum(std::string_view a_key, const Row (&a_table)[N], E a_value) {
    Set(a_key, std::string{NameOf(a_table, a_value)});
  }
  template <class Row, std::size_t N, class E>
  void WriteEnumIf(std::string_view a_key, const Row (&a_table)[N], E a_value,
                   E a_default) {
    if (a_value != a_default)
      Set(a_key, std::string{NameOf(a_table, a_value)});
  }
};

[[nodiscard]] std::string DumpDocument(const json &a_root);
}
