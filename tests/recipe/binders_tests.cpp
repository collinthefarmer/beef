#include "recipe/Binders.h"
#include "test_support.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
bool Names(const std::vector<Diagnostic> &a_diagnostics,
           std::string_view a_fragment) {
  return std::ranges::any_of(a_diagnostics, [&](const Diagnostic &d) {
    return d.message.contains(a_fragment);
  });
}

void DocumentParsingIsGuarded() {
  std::vector<Diagnostic> diagnostics;
  const Reporter ctx{diagnostics, "file"};
  Check(ParseObjectDocument(R"({"a": 1})", ctx).has_value() &&
            diagnostics.empty(),
        "an object document parses cleanly");
  Check(!ParseObjectDocument("[]", ctx).has_value() &&
            Names(diagnostics, "not a JSON object"),
        "an array document is refused by name");
  diagnostics.clear();
  Check(!ParseObjectDocument("", ctx).has_value() &&
            Names(diagnostics, "not a JSON object"),
        "an empty document is refused by name");
  diagnostics.clear();
  Check(!ParseObjectDocument(std::string(kMaxRecipeDepth + 2, '{'), ctx)
                .has_value() &&
            Names(diagnostics, "nested deeper than"),
        "a document past the depth cap is refused before parsing");
  diagnostics.clear();
  Check(ParseObjectDocument(R"({"a": 1, "a": 2})", ctx).has_value() &&
            Names(diagnostics, "duplicate key 'a'"),
        "a duplicate key is reported and the document still parses");
}

void ReaderReportsUnknownKeys() {
  std::vector<Diagnostic> diagnostics;
  const json object = json::parse(R"({"known": 1, "stray": true})");
  Reader r(object, Reporter{diagnostics, "row"});
  Equal(r.Integer("known").value_or(0), 1, "a known key reads");
  r.Finish();
  Check(diagnostics.size() == 1 && diagnostics[0].where == "row" &&
            diagnostics[0].message == "unknown key 'stray'",
        "an unconsumed key is an error at the row's where");
}

void ReaderTypedGettersRefuseWrongTypes() {
  std::vector<Diagnostic> diagnostics;
  const json object = json::parse(
      R"({"n": "x", "s": 2, "b": 1, "list": [1], "slot": "nowhere"})");
  Reader r(object, Reporter{diagnostics, "row"});
  Check(!r.Number("n").has_value() &&
            Names(diagnostics, "'n' must be a number"),
        "a string is not a number");
  Check(!r.String("s").has_value() &&
            Names(diagnostics, "'s' must be a string"),
        "a number is not a string");
  Check(!r.Boolean("b").has_value() &&
            Names(diagnostics, "'b' must be true or false"),
        "a number is not a boolean");
  Check(!r.Strings("list", 4).has_value() &&
            Names(diagnostics, "'list' entries must be strings"),
        "a list of numbers is not a list of strings");
  Check(!r.BipedSlot("slot").has_value() &&
            Names(diagnostics, "unknown biped slot name 'nowhere'"),
        "an unknown slot name is named");
  Check(!r.Number("absent").has_value(), "an absent key reads as nothing");
  Equal(diagnostics.size(), 5u, "each refusal is one diagnostic");
}

void SourceKindsRoundTrip() {
  std::vector<Diagnostic> diagnostics;
  const Reporter ctx{diagnostics, "source s"};
  for (const std::string_view text :
       {R"({"material": "roughness"})", R"({"uv": "u"})",
        R"({"bake": {"partition": "body"}})", R"({"distance": "NPC Root"})"}) {
    const json object = json::parse(text);
    Reader r(object, ctx);
    const auto kind = ParseSourceKind(r);
    Check(kind.has_value(),
          std::string{"the source parses: "} + std::string{text});
    if (kind) {
      Equal(SourceKindToJson(*kind).dump(), object.dump(),
            "the source serializes to its own text");
    }
  }
  Check(diagnostics.empty(), "no source produced a diagnostic");
  const json bogus = json::parse(R"({"bogus": 1})");
  Reader r(bogus, ctx);
  Check(!ParseSourceKind(r).has_value() &&
            Names(diagnostics, "unknown source kind 'bogus'"),
        "an unknown kind is named");
}

void SlotWordsRoundTrip() {
  Equal(BipedSlotToJson(BipedSlot{32}).dump(), std::string{"\"body\""},
        "a named slot writes its name");
  Equal(BipedSlotToJson(BipedSlot{45}).dump(), std::string{"45"},
        "an unnamed slot writes its number");
}
}

int main() {
  DocumentParsingIsGuarded();
  ReaderReportsUnknownKeys();
  ReaderTypedGettersRefuseWrongTypes();
  SourceKindsRoundTrip();
  SlotWordsRoundTrip();
  return test::Finish("recipe binders");
}
