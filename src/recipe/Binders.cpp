#include "recipe/Binders.h"

#include <charconv>
#include <cstdlib>
#include <functional>

namespace BetterEnchantmentEffects {
bool RowCapReached(std::size_t a_count, const Reporter &a_ctx,
                   std::string_view a_what, std::size_t a_cap) {
  if (a_count >= a_cap) {
    a_ctx.Error(std::format("'{}' has more than {} entries", a_what, a_cap));
    return true;
  }
  return false;
}

std::size_t MaxNestingDepth(std::string_view a_json) noexcept {
  std::size_t depth = 0;
  std::size_t deepest = 0;
  bool inString = false;
  bool escaped = false;
  for (const char c : a_json) {
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{' || c == '[') {
      ++depth;
      deepest = std::max(deepest, depth);
    } else if ((c == '}' || c == ']') && depth > 0) {
      --depth;
    }
  }
  return deepest;
}

namespace {
struct DuplicateFinder {
  std::vector<std::unordered_set<std::string>> scopes;
  std::vector<std::string> duplicates;

  bool operator()(int, json::parse_event_t a_event, json &a_parsed) {
    switch (a_event) {
    case json::parse_event_t::object_start:
      scopes.emplace_back();
      break;
    case json::parse_event_t::object_end:
      if (!scopes.empty()) {
        scopes.pop_back();
      }
      break;
    case json::parse_event_t::key:
      if (!scopes.empty() && a_parsed.is_string() &&
          !scopes.back().insert(a_parsed.get<std::string>()).second) {
        duplicates.push_back(a_parsed.get<std::string>());
      }
      break;
    default:
      break;
    }
    return true;
  }
};
}

std::optional<json> ParseObjectDocument(std::string_view a_json,
                                        const Reporter &a_ctx) {
  if (MaxNestingDepth(a_json) > kMaxRecipeDepth) {
    a_ctx.Error(std::format("nested deeper than {} levels", kMaxRecipeDepth));
    return std::nullopt;
  }
  DuplicateFinder finder;
  json root = json::parse(a_json, std::ref(finder), false, true);
  if (root.is_discarded() || !root.is_object()) {
    a_ctx.Error("not a JSON object (a syntax error, or the file is not what "
                "this loader reads)");
    return std::nullopt;
  }
  for (const auto &d : finder.duplicates) {
    a_ctx.Error(std::format("duplicate key '{}'", d));
  }
  return root;
}

std::optional<KindEntry>
OneKey(const json &a_j, const Reporter &a_ctx, std::string_view a_what,
       std::initializer_list<std::string_view> a_common) {
  if (!a_j.is_object()) {
    a_ctx.Error(std::format("{} must be an object with one kind key", a_what));
    return std::nullopt;
  }
  std::optional<KindEntry> found;
  for (const auto &[key, value] : a_j.items()) {
    if (std::ranges::find(a_common, key) != a_common.end()) {
      continue;
    }
    if (found) {
      a_ctx.Error(std::format("{} has two kind keys, '{}' and '{}'", a_what,
                              found->key, key));
      return std::nullopt;
    }
    found = KindEntry{key, &value};
  }
  if (!found) {
    a_ctx.Error(std::format("{} has no kind key", a_what));
  }
  return found;
}

json Num(float a_value) {
  char buffer[32];
  const auto r = std::to_chars(buffer, buffer + sizeof(buffer), a_value);
  return json(std::strtod(std::string(buffer, r.ptr).c_str(), nullptr));
}

json ParamToJson(const Param &a_param) {
  return Match(
      a_param, [](float f) { return Num(f); },
      [](const Ref &r) { return json("@" + r.name); });
}

json ValueToJson(const Value &a_value) {
  return Match(
      a_value, [](float f) { return Num(f); },
      [](const Vec2 &v) { return json::array({Num(v.x), Num(v.y)}); },
      [](const Vec3 &v) {
        return json::array({Num(v.x), Num(v.y), Num(v.z)});
      });
}

json PointToJson(const Vec3 &a_v) {
  return json::array({Num(a_v.x), Num(a_v.y), Num(a_v.z)});
}

json BipedSlotToJson(std::uint32_t a_slot) {
  const auto name = BipedSlotName(a_slot);
  return name ? json(std::string{*name}) : json(a_slot);
}

std::string DumpDocument(const json &a_root) { return a_root.dump(2) + "\n"; }
}
