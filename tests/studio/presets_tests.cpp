#include "studio/Presets.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;
using test::Equal;

namespace {
bool Names(const PresetsLoadResult &a_result, std::string_view a_where,
           std::string_view a_fragment) {
  return std::ranges::any_of(a_result.diagnostics, [&](const Diagnostic &d) {
    return d.severity == Severity::kError && d.where == a_where &&
           d.message.contains(a_fragment);
  });
}

std::string Seen(const PresetsLoadResult &a_result) {
  std::string seen;
  for (const Diagnostic &d : a_result.diagnostics) {
    seen += std::format(" [{}: {}]", d.where, d.message);
  }
  return seen;
}

void ExpectError(
    std::string_view a_json, std::string_view a_what, std::string_view a_where,
    std::string_view a_fragment,
    const std::source_location &a_loc = std::source_location::current()) {
  const PresetsLoadResult result = ParsePresets(a_json);
  Check(result.HasErrors() && Names(result, a_where, a_fragment),
        std::format("{} is reported at '{}' naming '{}'; saw{}", a_what,
                    a_where, a_fragment, Seen(result)),
        a_loc);
}

void WellFormedFileParses() {
  const PresetsLoadResult ok = ParsePresets(R"({
    "presets": [
      { "name": "leather",
        "expression": "(1 - @m) * @r",
        "sources": { "m": { "material": "metallic" }, "r": { "material": "roughness" } } },
      { "name": "chest", "partition": "body", "bones": ["NPC Spine2 [Spn2]"] },
      { "name": "ring", "partition": 40 }
    ]
  })");
  Check(!ok.HasErrors(),
        std::format("a well-formed preset file parses;{}", Seen(ok)));
  if (!ok.presets) {
    return;
  }
  Equal(ok.presets->presets.size(), 3u, "every preset is read");
  Check(ok.presets->presets[0].name == "leather" &&
            ok.presets->presets[0].sources.size() == 2 &&
            ok.presets->presets[0].expression == "(1 - @m) * @r",
        "the material preset keeps its expression and sources");
  Check(ok.presets->presets[1].partition == BipedSlot{32} &&
            ok.presets->presets[1].bones.size() == 1,
        "the spatial preset resolves its partition name and bones");
  Check(ok.presets->presets[2].partition == BipedSlot{40},
        "a numeric partition is kept as the slot number");
}

void EverySourceKindIsAccepted() {
  const PresetsLoadResult ok = ParsePresets(R"({
    "presets": [
      { "name": "wide", "expression": "@u + @d + @i",
        "sources": {
          "u": { "uv": "v" },
          "d": { "distance": "NPC Root [Root]" },
          "i": { "image": { "path": "Effects\\X.dds" } } } }
    ]
  })");
  Check(
      !ok.HasErrors() && ok.presets && ok.presets->presets.size() == 1 &&
          ok.presets->presets[0].sources.size() == 3,
      std::format("uv, distance and image sources are accepted;{}", Seen(ok)));
}

void RoundTripsTheShippedFile() {
  const std::filesystem::path path =
      std::filesystem::path{"presets"} / "regions.json";
  const std::string text = test::ReadFile(path);
  Check(!text.empty(), "the shipped presets file is readable");
  const PresetsLoadResult loaded = ParsePresets(text);
  Check(!loaded.HasErrors(),
        std::format("the shipped presets file parses without errors;{}",
                    Seen(loaded)));
  if (!loaded.presets) {
    return;
  }
  Check(loaded.presets->presets.size() >= 20,
        "the shipped file carries the region and material presets");
  Equal(SerializePresets(*loaded.presets), text,
        "the shipped presets file round-trips byte-identical");
}

void SerializationIsIdempotent() {
  const PresetsLoadResult first = ParsePresets(R"({"presets":[
    {"name":"a","partition":"hands","bones":["NPC L Hand [LHnd]"]},
    {"name":"b","expression":"@r","sources":{"r":{"material":"roughness"}}}]})");
  Check(first.presets.has_value(), "the compact file parses");
  if (!first.presets) {
    return;
  }
  const std::string once = SerializePresets(*first.presets);
  const PresetsLoadResult second = ParsePresets(once);
  Check(second.presets.has_value() && !second.HasErrors(),
        "the serialized file parses again");
  if (second.presets) {
    Equal(SerializePresets(*second.presets), once,
          "serialization is idempotent");
    Equal(second.presets->presets.size(), 2u,
          "both presets survive the round trip");
  }
}

void MalformedInputIsNamed() {
  ExpectError("[]", "a non-object file", "file", "not a JSON object");
  ExpectError("not json", "non-JSON input", "file", "not a JSON object");
  ExpectError(R"({"presets":[{"name":"empty"}]})",
              "a preset with no expression, partition or bones", "preset empty",
              "needs an expression");
  ExpectError(R"({"presets":[{"expression":"1"}]})", "a preset with no name",
              "preset 0", "'name' is required");
  ExpectError(R"({"presets":[{"name":"1bad","expression":"1"}]})",
              "a preset name that is not an identifier", "preset 1bad",
              "identifier");
  ExpectError(R"({"presets":[{"name":"p","expression":"1","extra":true}]})",
              "an unknown preset key", "preset p", "unknown key 'extra'");
  ExpectError(R"({"presets":[{"name":"p","expression":"1"}],"note":1})",
              "an unknown top-level key", "presets", "unknown key 'note'");
  ExpectError(R"({"presets":[{"name":"p","partition":"nowhere"}]})",
              "an unknown partition name", "preset p", "unknown biped slot");
  ExpectError(R"({"presets":[{"name":"p","partition":12}]})",
              "a partition number outside the biped range", "preset p",
              "30..61");
  ExpectError(R"({"presets":[{"name":"p","bones":"NPC Head [Head]"}]})",
              "bones that are not an array", "preset p",
              "must be an array of strings");
  ExpectError(R"({"presets":[{"name":"p","expression":"1 +"}]})",
              "an expression that does not parse", "preset p", "'expression'");
  ExpectError(
      R"({"presets":[{"name":"p","expression":"@x","sources":{"x":{"bogus":1}}}]})",
      "an unknown source kind", "preset p source x", "unknown source kind");
  ExpectError(R"({"presets":{"name":"p"}})", "presets that are not an array",
              "presets", "must be an array");
  ExpectError(R"({"presets":[{"name":"p","name":"q","expression":"1"}]})",
              "a duplicate key", "file", "duplicate key 'name'");

  std::string bones;
  for (std::size_t i = 0; i <= kMaxPresetBones; ++i) {
    bones += std::format("{}\"b{}\"", bones.empty() ? "" : ",", i);
  }
  ExpectError(
      std::format(R"({{"presets":[{{"name":"big","bones":[{}]}}]}})", bones),
      "more bones than the cap", "preset big", "more than");

  std::string entries;
  for (std::size_t i = 0; i <= kMaxPresets; ++i) {
    entries += std::format("{}{{\"name\":\"p{}\",\"expression\":\"1\"}}",
                           entries.empty() ? "" : ",", i);
  }
  const PresetsLoadResult tooMany =
      ParsePresets(std::format(R"({{"presets":[{}]}})", entries));
  Check(tooMany.HasErrors() && Names(tooMany, "presets", "more than"),
        "the preset count cap is reported");
  Check(tooMany.presets && tooMany.presets->presets.size() == kMaxPresets,
        "the presets under the cap are still read");
}

void ErrorsKeepTheHealthyPresets() {
  const PresetsLoadResult mixed = ParsePresets(R"({"presets":[
    {"name":"good","expression":"1"},
    {"name":"bad","partition":"nowhere"},
    {"name":"alsoGood","partition":"body"}]})");
  Check(mixed.HasErrors(), "one bad preset makes the file report errors");
  Check(mixed.presets && mixed.presets->presets.size() == 2,
        "the healthy presets are still loaded");
  Check(Names(mixed, "preset bad", "unknown biped slot"),
        "the bad preset is named in its own where");
}

void MissingArrayYieldsNothing() {
  const PresetsLoadResult none = ParsePresets(R"({})");
  Check(!none.HasErrors() && none.presets && none.presets->presets.empty(),
        "a file without a presets array yields no presets, not an error");
}
}

int main() {
  WellFormedFileParses();
  EverySourceKindIsAccepted();
  RoundTripsTheShippedFile();
  SerializationIsIdempotent();
  MalformedInputIsNamed();
  ErrorsKeepTheHealthyPresets();
  MissingArrayYieldsNothing();
  return test::Finish("studio_presets");
}
