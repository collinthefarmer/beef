#include "recipe/Recipe.h"
#include "recipe/Words.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
void RoundTripBytes(std::string_view a_name) {
  const std::filesystem::path path =
      test::Fixtures() / "recipes" / (std::string{a_name} + ".json");
  const std::string text = test::ReadFile(path);
  Check(!text.empty(), std::format("fixture {} is readable", a_name));

  const LoadResult loaded = ParseRecipe(text, a_name);
  Check(loaded.recipe.has_value(),
        std::format("fixture {} parses to a recipe", a_name));
  Check(!loaded.HasErrors(),
        std::format("fixture {} parses without errors", a_name));
  if (!loaded.recipe) {
    return;
  }
  const std::string written = SerializeRecipe(*loaded.recipe);
  Check(written == text,
        std::format("fixture {} round-trips byte-identical", a_name));
}

Recipe StableRoundTrip(std::string_view a_json, std::string_view a_id,
                       std::string_view a_what) {
  const LoadResult first = ParseRecipe(a_json, a_id);
  Check(first.recipe.has_value(), std::format("{} parses", a_what));
  if (!first.recipe) {
    return {};
  }
  const std::string once = SerializeRecipe(*first.recipe);
  const LoadResult second = ParseRecipe(once, a_id);
  Check(second.recipe.has_value(), std::format("{} re-parses", a_what));
  if (!second.recipe) {
    return *first.recipe;
  }
  Check(*first.recipe == *second.recipe,
        std::format("{} round-trips to an equal recipe", a_what));
  Check(SerializeRecipe(*second.recipe) == once,
        std::format("{} serialization is idempotent", a_what));
  return *second.recipe;
}

void ExpectError(
    std::string_view a_json, std::string_view a_what,
    std::string_view a_expectedMessageFragment,
    const std::source_location &a_loc = std::source_location::current()) {
  const LoadResult loaded = ParseRecipe(a_json, "garbage");
  const bool named = std::ranges::any_of(
      loaded.diagnostics, [&](const Diagnostic &a_diagnostic) {
        return a_diagnostic.severity == Severity::kError &&
               a_diagnostic.message.contains(a_expectedMessageFragment);
      });
  std::string seen;
  for (const Diagnostic &diagnostic : loaded.diagnostics) {
    seen += std::format(" [{}: {}]", diagnostic.where, diagnostic.message);
  }
  Check(loaded.HasErrors() && named,
        std::format("{} is reported as an error naming '{}'; saw{}", a_what,
                    a_expectedMessageFragment, seen),
        a_loc);
}

std::vector<std::string> FixtureRecipeNames() {
  std::vector<std::string> names;
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator{test::Fixtures() / "recipes"}) {
    if (entry.path().extension() == ".json") {
      names.push_back(entry.path().stem().string());
    }
  }
  std::ranges::sort(names);
  return names;
}
}

int main() {
  Recipe recipe;
  recipe.id = "example";
  recipe.signals.push_back(Signal{"glow", ConstantSignal{1.0f}, std::nullopt});
  Check(recipe.signals.size() == 1, "a signal is stored");

  const SignalKind kind = recipe.signals.front().kind;
  const bool isConstant = Match(
      kind, [](const ConstantSignal &) { return true; },
      [](const auto &) { return false; });
  Check(isConstant, "the signal holds a constant");

  {
    Recipe dup;
    dup.id = "dup";
    dup.signals.push_back(Signal{"shared", ConstantSignal{0.0f}, std::nullopt});
    dup.sources.push_back(Source{"shared", MaterialSource{}});
    const LoadResult loaded = ParseRecipe(SerializeRecipe(dup), "dup");
    Check(loaded.recipe.has_value() && loaded.recipe->signals.size() == 1 &&
              loaded.recipe->sources.empty(),
          "load drops a row whose name a prior row already claimed");
    Check(loaded.HasErrors(), "the dropped duplicate name is reported");
  }

  Check(kKeyKindCount == std::size(kKeyKinds),
        "the key-kind table has a row per kind");
  Check(kSignalKindCount == std::size(kSignalKinds),
        "the signal-kind table has a row per kind");
  Check(kSlotCount == std::size(kSlots), "the slot table has a row per slot");
  Check(NameOf(kBlends, Blend::kAdd) == "add", "blend add names itself");
  Check(FromName(kBlends, "screen") == Blend::kScreen,
        "blend screen parses back");

  Check(std::size(kBipedSlots) == 11, "the biped-slot table is exposed");
  Check(kBipedSlots[2].slot == BipedSlot{32} && kBipedSlots[2].name == "body",
        "slot 32 is body");

  Check(Recipe{} == Recipe{}, "two empty recipes compare equal");

  {
    Check(DefaultSlotResolution(Slot::kNormal) == Resolution::kFull &&
              DefaultSlotResolution(Slot::kHeight) == Resolution::kFull,
          "normal and height default to full resolution");
    Check(DefaultSlotResolution(Slot::kDiffuse) == Resolution::kHalf &&
              DefaultSlotResolution(Slot::kRmaos) == Resolution::kHalf,
          "diffuse and rmaos default to half resolution");
    Check(DefaultSlotResolution(Slot::kEmissive) == Resolution::kQuarter &&
              DefaultSlotResolution(Slot::kGlint) == Resolution::kQuarter,
          "emissive and the response slots default to quarter resolution");
    Check(ResolutionDivisor(Resolution::kFull) == 1 &&
              ResolutionDivisor(Resolution::kHalf) == 2 &&
              ResolutionDivisor(Resolution::kQuarter) == 4,
          "the resolution divisors are 1, 2 and 4");
    Check(ParseResolution("quarter") == Resolution::kQuarter &&
              ResolutionName(Resolution::kQuarter) == "quarter",
          "a resolution name round-trips");

    Recipe withRes;
    withRes.id = "res";
    SurfaceOutput out;
    out.slot = Slot::kEmissive;
    out.resolution = Resolution::kHalf;
    out.scalars.strength = 1.0f;
    withRes.outputs.push_back(out);
    const Recipe back = StableRoundTrip(SerializeRecipe(withRes), "res",
                                        "an output resolution");
    const SurfaceOutput *readback =
        back.outputs.empty() ? nullptr
                             : Get<SurfaceOutput>(back.outputs.front());
    Check(readback != nullptr && readback->resolution == Resolution::kHalf,
          "the per-output resolution survives the round trip");
  }

  {
    Check(ParseOverrideMode("sampled") == OverrideMode::kSampled &&
              OverrideModeName(OverrideMode::kSampled) == "sampled",
          "an override mode name round-trips");
    Recipe defaulted;
    defaulted.id = "def";
    Check(defaulted.overrideMode == OverrideMode::kStack,
          "a recipe defaults to the stack override mode");
    Check(!SerializeRecipe(defaulted).contains("override"),
          "the default override mode is omitted from the file");

    Recipe sampled;
    sampled.id = "samp";
    sampled.overrideMode = OverrideMode::kReplace;
    const Recipe back =
        StableRoundTrip(SerializeRecipe(sampled), "samp", "an override mode");
    Check(back.overrideMode == OverrideMode::kReplace,
          "the recipe override mode survives the round trip");
  }

  const std::vector<std::string> fixtures = FixtureRecipeNames();
  Check(fixtures.size() >= 7,
        std::format("the recipe fixture folder holds at least the seven "
                    "vanilla effect shaders (found {})",
                    fixtures.size()));
  for (const std::string &name : fixtures) {
    RoundTripBytes(name);
  }

  {
    const std::string example =
        test::ReadFile(std::filesystem::path{"schema/example-magicka.json"});
    Check(!example.empty(), "example-magicka.json is readable");
    StableRoundTrip(example, "example-magicka", "example-magicka.json");
  }

  {
    constexpr std::string_view everyKind = R"({
  "format": 1,
  "keys": ["default"],
  "signals": {
    "c":          { "constant": 0.5 },
    "vecConst":   { "constant": [0.1, 0.2, 0.3] },
    "trig":       { "trigger": { "event": "hit.received", "lifetime": 1.0, "max": 2 } },
    "plug":       { "trigger": { "plugin": "MyMod|effect" } },
    "gate":       { "trigger": { "when": "@c", "value": "@c" } },
    "pulseSig":   { "pulse": { "base": 0.0, "amplitude": 1.0, "period": 2.0, "phase": 0.25, "waveform": "square" } },
    "rampSig":    { "ramp": { "from": 0.0, "to": 1.0, "seconds": 3.0 } },
    "efshSig":    { "efsh": { "field": "fillAlpha", "record": "EnchArmorMagickaFXS" } },
    "avSig":      { "av": { "of": "Health", "measure": "max" } },
    "avCur":      { "av": "Magicka" },
    "stateSig":   { "actorState": "sneaking" },
    "enchSig":    { "enchantment": "magnitude" },
    "payloadSig": { "payload": { "trigger": "@trig", "field": "position" } },
    "counterSig": { "counter": { "trigger": "@trig", "reset": "@trig", "cap": 5.0 } },
    "accumSig":   { "accumulate": { "trigger": "@trig", "decay": 0.5 } },
    "noiseSig":   { "noise": { "frequency": 2.0, "amplitude": 0.5, "seed": 7 } },
    "gradSig":    { "gradient": { "t": "@c", "stops": [ { "at": 0.0, "color": [0.0, 0.0, 0.0] }, { "at": 1.0, "color": [1.0, 1.0, 1.0] } ] } },
    "deltaSig":   { "delta": "@c" },
    "smoothSig":  { "smooth": { "of": "@c", "seconds": 0.5 } },
    "exprSig":    { "expr": "1 + 2" }
  },
  "sources": {
    "img":      { "image": { "path": "Effects\\X.dds", "channel": "luma", "space": "mesh", "mirror": [true, false], "transpose": true, "mip": 1.0 } },
    "mat":      { "material": "roughness" },
    "bakePos":  { "bake": "position" },
    "bakePart": { "bake": { "partition": "body" } },
    "bakeBone": { "bake": { "boneWeight": ["NPC Spine", "NPC Spine1"] } },
    "uvSrc":    { "uv": "v" },
    "distNode": { "distance": "NPC Root [Root]" },
    "distPt":   { "distance": { "from": [1.0, 2.0, 3.0] } },
    "ripSrc":   { "ripple": { "trigger": "@trig", "speed": 90.0, "width": 8.0, "decay": 1.2, "shape": "disc" } },
    "clusters": { "materialClusters": { "clusters": 3, "weights": { "roughness": 2.0, "luma": 0.5 }, "seed": 9, "iterations": 64 } }
  }
})";
    const Recipe r =
        StableRoundTrip(everyKind, "every-kind", "every-kind recipe");

    std::array<bool, kSignalKindCount> signalSeen{};
    for (const Signal &s : r.signals) {
      signalSeen[static_cast<std::size_t>(SignalKindOf(s.kind))] = true;
    }
    bool allSignals = true;
    for (const bool seen : signalSeen) {
      allSignals = allSignals && seen;
    }
    Check(allSignals, "every one of the 16 signal kinds round-trips");

    std::array<bool, std::variant_size_v<SourceKind>> sourceSeen{};
    for (const Source &s : r.sources) {
      sourceSeen[s.kind.index()] = true;
    }
    bool allSources = true;
    for (const bool seen : sourceSeen) {
      allSources = allSources && seen;
    }
    Check(allSources, "every one of the 7 source kinds round-trips");
  }

  {
    constexpr std::string_view withOutputs = R"({
  "format": 1,
  "keys": [ { "effectShader": "EnchArmorMagickaFXS" }, "default", { "keyword": "MagicDisallowEnchanting" }, { "material": "*ebony*" } ],
  "priority": 5,
  "clock": { "speed": 2.0 },
  "author": "tester",
  "version": "1.2.3",
  "signals": { "glow": { "constant": 1.0 }, "hue": { "constant": [0.2, 0.4, 1.0] } },
  "sources": { "fill": { "image": { "path": "a.dds" } } },
  "masks": { "all": "1" },
  "outputs": [
    { "target": "material", "slot": "emissive", "strength": "@glow", "replace": true,
      "selector": [ { "geometry": "*body*" } ],
      "stack": [ { "source": "@fill", "blend": "screen", "opacity": 0.5, "color": "@hue", "mask": "@all", "channels": "rgb" } ] },
    { "target": "light", "bones": { "named": ["NPC Head"] }, "offset": [0.0, 1.0, 0.0],
      "color": [1.0, 1.0, 1.0], "intensity": 2.0, "size": 1.5, "cutoff": 0.1, "shadow": true }
  ],
  "shell": { "material": "vanilla", "blend": "alpha", "depthBias": false, "alphaTest": 0.5, "rimPower": 2.0, "emissive": 0.3,
    "pose": { "inflate": [0.1, 0.1, 0.1], "scale": 1.1, "spin": 0.5 } },
  "variants": [
    { "name": "ebony", "key": { "armor": "ArmorEbonyCuirass" }, "overrides": { "glow": 1.5 } },
    { "name": "bySelector", "key": { "selector": [ { "texture": "*gold*" } ] }, "overrides": {} }
  ]
})";
    StableRoundTrip(withOutputs, "with-outputs",
                    "outputs/shell/variants recipe");
  }

  ExpectError("", "empty input", "not a JSON object");
  ExpectError("not json", "non-JSON input", "not a JSON object");
  ExpectError("[]", "a top-level array", "not a JSON object");
  ExpectError("42", "a bare number", "not a JSON object");
  ExpectError(R"({"format": 1})", "a recipe with no keys",
              "'keys' is required");
  ExpectError(R"({"format": 1, "keys": []})", "a recipe with empty keys",
              "non-empty array");
  ExpectError(R"({"format": 9999, "keys": ["default"]})",
              "a future format version", "newer than this loader");
  ExpectError(R"({"keys": ["default"]})", "a recipe with no format",
              "'format' is required");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "signals": { "x": { "bogus": 1 } }})",
      "an unknown signal kind", "unknown signal kind 'bogus'");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "sources": { "x": { "bogus": 1 } }})",
      "an unknown source kind", "unknown source kind 'bogus'");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "signals": { "x": { "efsh": { "field": "fillAlpha" } } }})",
      "an efsh signal missing its record", "'efsh' needs 'record'");
  ExpectError(R"({"format": 1, "format": 1, "keys": ["default"]})",
              "a duplicate key", "duplicate key 'format'");
  ExpectError(R"({"format": 1, "keys": ["default"], "nonsense": true})",
              "an unknown top-level key", "unknown key 'nonsense'");
  ExpectError(std::string(64, '['), "input nested past the depth cap",
              "nested deeper than");

  {
    const LoadResult badFormat =
        ParseRecipe(R"({"format": 9999, "keys": ["default"]})", "held");
    Check(badFormat.recipe.has_value() && badFormat.HasRecipeErrors(),
          "a newer format still yields a recipe for the menu, held back by a "
          "recipe-level error");
    Check(badFormat.recipe && badFormat.recipe->keys.size() == 1,
          "the rest of a newer-format file is still read");
    const LoadResult badKeys =
        ParseRecipe(R"({"format": 1, "keys": []})", "held");
    Check(badKeys.HasRecipeErrors(), "empty keys are a recipe-level error");
    const LoadResult badSignal = ParseRecipe(
        R"({"format": 1, "keys": ["default"], "signals": { "x": { "bogus": 1 } }})",
        "row");
    Check(badSignal.HasErrors() && !badSignal.HasRecipeErrors(),
          "one bad signal is a row error and does not hold the recipe back");
    Check(
        RowLevel(MakeDiagnostic(Severity::kError, SignalWhere("x"), "m")) &&
            RowLevel(MakeDiagnostic(Severity::kError, LayerWhere(1, 0), "m")) &&
            !RowLevel(MakeDiagnostic(Severity::kError, "recipe", "m")) &&
            !RowLevel(MakeDiagnostic(Severity::kError, "clock", "m")) &&
            !RowLevel(
                MakeDiagnostic(Severity::kError, KeyWhere(RecipeKey{}), "m")),
        "row-level wheres are the signal, curve, source, mask, output and "
        "variant rows; keys, clock and the recipe itself are not");
  }

  return test::Finish("recipe");
}
