// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"
#include "recipe/RecipeGraph.h"
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
  {
    Check(UnitHash("Skyrim.esm|0x0001A2B3") ==
                  UnitHash("Skyrim.esm|0x0001A2B3") &&
              UnitHash("Skyrim.esm|0x0001A2B3") !=
                  UnitHash("Skyrim.esm|0x0001A2B4"),
          "the unit hash is stable and separates neighbouring keys");
    bool inRange = true;
    for (int i = 0; i < 1000; ++i) {
      const float h = UnitHash(std::to_string(i));
      inRange = inRange && h >= 0.0f && h < 1.0f;
    }
    Check(inRange, "the unit hash stays in zero to one");
    const Recipe picked = StableRoundTrip(
        R"({"format":1,"keys":["default"],)"
        R"("signals":{"which":{"enchantment":"idHash"}},)"
        R"("sources":{"zone":{"material":{"groups":{"pick":"@which",)"
        R"("areaBias":0.5}}}}})",
        "picked", "a cluster pick driven by the enchantment hash");
    const auto *clusters =
        picked.sources.empty()
            ? nullptr
            : Get<MaterialGroupsSource>(picked.sources.front().kind);
    Check(clusters && clusters->pick == Param{Ref{"which"}} &&
              clusters->areaBias == Param{0.5f},
          "the pick and area bias survive a round trip");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"zone":)"
                R"({"material":{"groups":{"pick":0.5,"areaBias":9}}}}})",
                "an area bias above eight", "areaBias");
    const LoadResult unpicked =
        ParseRecipe(R"({"format":1,"keys":["default"],"sources":{"zone":)"
                    R"({"material":{"groups":{"areaBias":0.5}}}}})",
                    "unpicked");
    Check(unpicked.recipe.has_value() && !unpicked.HasErrors() &&
              std::ranges::any_of(
                  unpicked.diagnostics,
                  [](const Diagnostic &a_diagnostic) {
                    return a_diagnostic.severity == Severity::kWarning &&
                           a_diagnostic.message.contains("without 'pick'");
                  }),
          "an area bias without a pick loads with a warning");
    const LoadResult plain =
        ParseRecipe(R"({"format":1,"keys":["default"],"sources":{"zone":)"
                    R"({"material":{"groups":{}}}}})",
                    "plain");
    Check(plain.recipe.has_value() &&
              std::ranges::none_of(plain.diagnostics,
                                   [](const Diagnostic &a_diagnostic) {
                                     return a_diagnostic.message.contains(
                                         "without 'pick'");
                                   }),
          "the default area bias without a pick does not warn");
  }
  {
    const LoadResult minimal =
        ParseRecipe(R"({"format":1,"keys":["default"]})", "defaults");
    Check(minimal.recipe.has_value() && !minimal.HasErrors(),
          "required fields alone form a valid recipe");
    if (minimal.recipe) {
      const Recipe defaults;
      Check(minimal.recipe->clock == defaults.clock &&
                minimal.recipe->mergeMode == defaults.mergeMode &&
                minimal.recipe->shell == defaults.shell &&
                !minimal.recipe->priority && minimal.recipe->outputs.empty(),
            "omitted optional settings retain recipe defaults");
      const LoadResult restored =
          ParseRecipe(SerializeRecipe(*minimal.recipe), "defaults");
      Check(restored.recipe == minimal.recipe && !restored.HasErrors(),
            "defaulted recipe survives serialization without semantic changes");
    }
  }
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
    bool allFull = true;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
      allFull = allFull && DefaultSlotResolution(static_cast<Slot>(slot)) ==
                               Resolution::kFull;
    }
    Check(allFull, "every slot defaults to full resolution");
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
    Check(ParseMergeMode("sampled") == MergeMode::kSampled &&
              MergeModeName(MergeMode::kSampled) == "sampled",
          "an override mode name round-trips");
    Recipe defaulted;
    defaulted.id = "def";
    Check(defaulted.mergeMode == MergeMode::kStack,
          "a recipe defaults to the stack override mode");
    Check(!SerializeRecipe(defaulted).contains("merge"),
          "the default override mode is omitted from the file");

    Recipe sampled;
    sampled.id = "samp";
    sampled.mergeMode = MergeMode::kReplace;
    const Recipe back =
        StableRoundTrip(SerializeRecipe(sampled), "samp", "an override mode");
    Check(back.mergeMode == MergeMode::kReplace,
          "the recipe override mode survives the round trip");
  }

  {
    Recipe noNotes;
    noNotes.id = "plain";
    Signal bare;
    bare.name = "glow";
    bare.kind = ConstantSignal{1.0f};
    noNotes.signals.push_back(bare);
    Check(!SerializeRecipe(noNotes).contains("note"),
          "an empty note is omitted from the file");

    Recipe noted;
    noted.id = "noted";
    Signal sig;
    sig.name = "glow";
    sig.kind = ConstantSignal{1.0f};
    sig.note = "drives the emissive pulse";
    noted.signals.push_back(sig);
    Source src;
    src.name = "tex";
    src.kind = MaterialSource{MaterialChannel::kDiffuseRgb};
    src.note = "base albedo";
    noted.sources.push_back(src);
    SurfaceOutput out;
    out.slot = Slot::kEmissive;
    out.scalars.strength = 1.0f;
    out.note = "the enchant's visible glow";
    Layer layer;
    layer.source = Ref{"tex"};
    layer.opacity = 1.0f;
    layer.note = "albedo tint";
    out.stack.push_back(layer);
    noted.outputs.push_back(out);
    noted.curves.push_back(Curve{"ease", "smoothstep(0, 1, x)", "soft ramp"});
    noted.masks.push_back(Mask{"body", "@tex", "limits to the cuirass"});

    const Recipe back =
        StableRoundTrip(SerializeRecipe(noted), "noted", "row notes");
    const SurfaceOutput *readOut =
        back.outputs.empty() ? nullptr
                             : Get<SurfaceOutput>(back.outputs.front());
    Check(!back.signals.empty() &&
              back.signals.front().note == "drives the emissive pulse",
          "a signal note survives the round trip");
    Check(!back.sources.empty() && back.sources.front().note == "base albedo",
          "a source note survives the round trip");
    Check(readOut != nullptr && readOut->note == "the enchant's visible glow",
          "an output note survives the round trip");
    Check(readOut != nullptr && !readOut->stack.empty() &&
              readOut->stack.front().note == "albedo tint",
          "a layer note survives the round trip");
    Check(!back.curves.empty() && back.curves.front().note == "soft ramp" &&
              back.curves.front().text == "smoothstep(0, 1, x)",
          "a curve note survives the round trip as the dual form");
    Check(!back.masks.empty() &&
              back.masks.front().note == "limits to the "
                                         "cuirass" &&
              back.masks.front().text == "@tex",
          "a mask note survives the round trip as the dual form");

    Recipe plainExpr;
    plainExpr.id = "plainexpr";
    plainExpr.curves.push_back(Curve{"ease", "x", ""});
    plainExpr.masks.push_back(Mask{"body", "1", ""});
    const std::string plainJson = SerializeRecipe(plainExpr);
    Check(plainJson.contains("\"ease\": \"x\"") &&
              plainJson.contains("\"body\": \"1\""),
          "a curve and mask without a note stay bare expression strings");
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
    "trig":       { "trigger": { "event": "hit.received", "anchor": { "node": "NPC Head [Head]" }, "lifetime": 1.0, "max": 2 } },
    "trigV":      { "trigger": { "event": "hit.received.position", "payload": "vec3", "anchor": "world", "lifetime": 1.0 } },
    "payloadV":   { "payload": "@trigV" },
    "plug":       { "trigger": { "plugin": "MyMod|effect" } },
    "gate":       { "trigger": { "when": "@c", "value": "@c" } },
    "waveSig":    { "wave": { "base": 0.0, "amplitude": 1.0, "period": 2.0, "phase": 0.25, "waveform": "square" } },
    "rampSig":    { "ramp": { "from": 0.0, "to": 1.0, "seconds": 3.0 } },
    "efshSig":    { "efsh": { "field": "fillAlpha", "record": "EnchArmorMagickaFXS" } },
    "avSig":      { "av": { "of": "Health", "measure": "max" } },
    "avCur":      { "av": "Magicka" },
    "stateSig":   { "actorState": "sneaking" },
    "enchSig":    { "enchantment": "magnitude" },
    "payloadSig": { "payload": "@trig" },
    "counterSig": { "counter": { "trigger": "@trig", "reset": "@trig", "cap": 5.0 } },
    "accumSig":   { "accumulate": { "trigger": "@trig", "decay": 0.5 } },
    "noiseSig":   { "noise": { "frequency": 2.0, "amplitude": 0.5, "seed": 7 } },
    "gradSig":    { "gradient": { "t": "@c", "stops": [ { "at": 0.0, "color": [0.0, 0.0, 0.0] }, { "at": 1.0, "color": [1.0, 1.0, 1.0] } ] } },
    "rateSig":    { "rate": "@c" },
    "toRootSig":  { "toRoot": "@posV" },
    "posV":       { "actorState": "position" },
    "smoothSig":  { "smooth": { "of": "@c", "seconds": 0.5 } },
    "exprSig":    { "expr": "1 + 2" },
    "itemSig":    { "item": "itemHash" },
    "chartsSig":  { "item": "chartCount" },
    "indexSig":   { "item": "geometryIndex" },
    "countSig":   { "item": "geometryCount" }
  },
  "sources": {
    "img":      { "image": { "path": "Effects\\X.dds", "channel": "luma", "space": "mesh", "mirror": [true, false], "transpose": true, "mip": 1.0 } },
    "mat":      { "material": "roughness" },
    "bakePos":  { "mesh": "position" },
    "bakePart": { "mesh": { "partition": "body" } },
    "bakeBone": { "mesh": { "boneWeight": ["NPC Spine", "NPC Spine1"] } },
    "bakeGeom": { "mesh": { "geometry": "BonemoldCuirass_1" } },
    "uvSrc":    { "mesh": "uv" },
    "distNode": { "distance": "NPC Root [Root]" },
    "ripSrc":   { "ripple": { "trigger": "@trig", "speed": 90.0, "width": 8.0, "decay": 1.2, "shape": "disc" } },
    "clusters": { "material": { "groups": { "merge": 0.45, "smoothing": 3 } } },
    "debugClusters": { "material": { "groups": { "palette": true } } },
    "lit": { "pick": { "seed": 0.4, "geometries": 2, "charts": "@countSig", "minCover": 0.1, "smoothing": 4 } }
  }
})";
    const Recipe r =
        StableRoundTrip(everyKind, "every-kind", "every-kind recipe");
    const Source *debugClusters = r.FindSource("debugClusters");
    const auto *debugKind = debugClusters
                                ? Get<MaterialGroupsSource>(debugClusters->kind)
                                : nullptr;
    Check(debugKind && debugKind->palette &&
              SourceType(*debugClusters) == ValueType::kVec3,
          "a palette cluster source round-trips as a colour");
    MaterialGroupsSource picked;
    picked.pick = Param{0.5f};
    Check(SourceHoldsIds(SourceKind{MaterialGroupsSource{}}) &&
              SourceHoldsIds(SourceKind{MeshSource{ChartBake{}}}) &&
              !SourceHoldsIds(SourceKind{picked}) &&
              !SourceHoldsIds(debugClusters->kind) &&
              !SourceHoldsIds(SourceKind{MaterialSource{}}),
          "cluster, part and chart id sources hold ids; picked, palette and "
          "material sources do not");
    const Source *legacy = r.FindSource("clusters");
    const auto *legacyKind =
        legacy ? Get<MaterialGroupsSource>(legacy->kind) : nullptr;
    Check(legacyKind && legacyKind->settings.merge == 0.45f &&
              legacyKind->settings.smoothing == 3 &&
              SourceType(*legacy) == ValueType::kScalar,
          "merge and smoothing round-trip and a plain cluster source is a "
          "scalar");

    std::array<bool, kSignalKindCount> signalSeen{};
    for (const Signal &s : r.signals) {
      signalSeen[static_cast<std::size_t>(SignalKindOf(s.kind))] = true;
    }
    bool allSignals = true;
    for (const bool seen : signalSeen) {
      allSignals = allSignals && seen;
    }
    Check(allSignals, "every one of the 17 signal kinds round-trips");

    std::array<bool, std::variant_size_v<SourceKind>> sourceSeen{};
    for (const Source &s : r.sources) {
      sourceSeen[s.kind.index()] = true;
    }
    bool allSources = true;
    for (const bool seen : sourceSeen) {
      allSources = allSources && seen;
    }
    Check(allSources, "every one of the 7 source kinds round-trips");
    const Source *lit = r.FindSource("lit");
    const auto *pick = lit ? Get<PickSource>(lit->kind) : nullptr;
    Check(
        pick && pick->seed == Param{0.4f} && pick->geometries == Param{2.0f} &&
            pick->charts == Param{Ref{"countSig"}} && pick->minCover == 0.1f &&
            pick->settings.smoothing == 4 && pick->areaBias == Param{1.0f} &&
            SourceType(*lit) == ValueType::kScalar,
        "a pick round-trips its seed, counts, cover and settings");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"p":)"
                R"({"pick":{"charts":2}}}})",
                "a pick without a seed", "seed");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"p":)"
                R"({"pick":{"seed":0.5,"minCover":2}}}})",
                "a pick cover above one", "minCover");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"p":)"
                R"({"pick":{"seed":0.5,"charts":0}}}})",
                "a pick of no charts", "charts");
    const auto itemFieldOf = [&](std::string_view a_name) {
      const auto found = std::ranges::find(r.signals, a_name, &Signal::name);
      const ItemSignal *item =
          found != r.signals.end() ? Get<ItemSignal>(found->kind) : nullptr;
      return item ? std::optional{item->field} : std::nullopt;
    };
    Check(itemFieldOf("itemSig") == ItemField::kItemHash &&
              itemFieldOf("chartsSig") == ItemField::kChartCount &&
              itemFieldOf("indexSig") == ItemField::kGeometryIndex &&
              itemFieldOf("countSig") == ItemField::kGeometryCount,
          "every item signal field round-trips");
    const Source *geometry = r.FindSource("bakeGeom");
    const MeshSource *geometryBake =
        geometry ? Get<MeshSource>(geometry->kind) : nullptr;
    const GeometryBake *named =
        geometryBake ? Get<GeometryBake>(geometryBake->bake) : nullptr;
    Check(named && named->geometry == "BonemoldCuirass_1",
          "a geometry bake round-trips with its geometry name");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"g":)"
                R"({"mesh":{"geometry":""}}}})",
                "a geometry bake with an empty name", "'geometry'");
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"g":)"
                R"({"mesh":"geometry"}}})",
                "a geometry bake without a name", "geometry");
    const LoadResult oldWord = ParseRecipe(
        R"({"format": 1, "sources": {"item": {"mesh": "itemHash"}}})", "old");
    Check(oldWord.HasErrors(), "the mesh word itemHash is no longer read");
    for (const auto &[retired, replacement] :
         {std::pair{R"({"bake": "uv"})", "'mesh'"},
          std::pair{R"({"materialClusters": {}})", "\"groups\""},
          std::pair{R"({"mesh": "componentId"})", "part"}}) {
      ExpectError(std::format(R"({{"format":1,"keys":["default"],)"
                              R"("sources":{{"s":{}}}}})",
                              retired),
                  retired, replacement);
    }
    ExpectError(R"({"format":1,"keys":["default"],"sources":{"s":)"
                R"({"material":{"roughness":1}}}})",
                "a material object other than groups", "groups");
  }

  {
    constexpr std::string_view withOutputs = R"({
  "format": 1,
  "keys": [ { "effectShader": "EnchArmorMagickaFXS" }, "default", "enchanted", { "keyword": "MagicDisallowEnchanting" }, { "material": "*ebony*" } ],
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

  {
    constexpr std::string_view matched = R"({
  "format": 1,
  "keys": [{ "magicEffect": "0x1~A.esp" }],
  "signals": {
    "matchedAlpha": { "efsh": { "field": "fillAlpha" } },
    "namedAlpha": { "efsh": { "field": "edgeColor", "record": "0x2~A.esp" } }
  },
  "sources": {
    "fill": { "image": { "effectShader": "fill", "channel": "a", "tile": [2.0, 2.0] } },
    "membrane": { "image": { "effectShader": "membrane" } },
    "file": { "image": { "path": "a.dds" } }
  }
})";
    const Recipe r =
        StableRoundTrip(matched, "matched", "matched-effect shader recipe");
    const Signal *matchedAlpha = r.FindSignal("matchedAlpha");
    const auto *unnamed =
        matchedAlpha ? Get<EfshSignal>(matchedAlpha->kind) : nullptr;
    Check(unnamed && !unnamed->record,
          "an efsh signal without a record reads as the matched effect");
    const Signal *namedAlpha = r.FindSignal("namedAlpha");
    const auto *named =
        namedAlpha ? Get<EfshSignal>(namedAlpha->kind) : nullptr;
    Check(named && named->record && named->record->text == "0x2~A.esp",
          "an efsh signal with a record keeps it");
    const Source *fill = r.FindSource("fill");
    const auto *fillImage = fill ? Get<ImageSource>(fill->kind) : nullptr;
    Check(fillImage &&
              fillImage->texture ==
                  ImageTexture{EffectShaderImage{EffectShaderTexture::kFill}} &&
              fillImage->channel == ImageChannel::kA && fillImage->tile,
          "an effect-shader image keeps its texture and image properties");
    const Source *membrane = r.FindSource("membrane");
    const auto *membraneImage =
        membrane ? Get<ImageSource>(membrane->kind) : nullptr;
    Check(membraneImage &&
              membraneImage->texture == ImageTexture{EffectShaderImage{
                                            EffectShaderTexture::kMembrane}},
          "a membrane image reads the membrane texture");
    const std::string written = SerializeRecipe(r);
    Check(written.contains(R"("effectShader": "membrane")") &&
              written.contains(R"("matchedAlpha": {
      "efsh": {
        "field": "fillAlpha"
      })"),
          "the writer spells the effect-shader image and omits an absent "
          "record");
  }

  ExpectError("", "empty input", "not a JSON object");
  ExpectError("not json", "non-JSON input", "not a JSON object");
  ExpectError("[]", "a top-level array", "not a JSON object");
  ExpectError("42", "a bare number", "not a JSON object");
  ExpectError(R"({"format": 1})", "a recipe with no keys",
              "'keys' is required");
  ExpectError(R"({"format": 1, "keys": []})", "a recipe with empty keys",
              "non-empty array");
  ExpectError(R"({"format": 0, "keys": ["default"]})",
              "an unsupported older format", "unsupported format");
  ExpectError(R"({"format": 4294967297, "keys": ["default"]})",
              "a format that would wrap to one", "integer in");
  ExpectError(R"({"format": 1, "keys": ["default"], "priority": 2147483648})",
              "an overflowing priority", "integer in");
  ExpectError(R"({"format": 1, "keys": ["default"], "priority": -2147483649})",
              "an underflowing priority", "integer in");
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
      R"({"format": 1, "keys": ["default"], "signals": { "x": { "efsh": { "record": "0x1~A.esp" } } }})",
      "an efsh signal missing its field", "'efsh' needs 'field'");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "sources": { "x": { "image": { "path": "a.dds", "effectShader": "fill" } } }})",
      "an image with both a path and an effect shader",
      "exactly one of 'path' and 'effectShader'");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "sources": { "x": { "image": { "channel": "r" } } }})",
      "an image with neither a path nor an effect shader",
      "exactly one of 'path' and 'effectShader'");
  ExpectError(
      R"({"format": 1, "keys": ["default"], "sources": { "x": { "image": { "effectShader": "holes" } } }})",
      "an image with an unknown effect shader texture",
      "'effectShader' is not one of");
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

  {
    Check(std::size(kActorStates) == kActorStateCount &&
              FromName(kActorStates, "resolutionLevel") ==
                  ActorStateKind::kResolutionLevel,
          "the actor state words cover every kind, resolutionLevel included");
    for (const Named<ActorStateKind> &state : kActorStates) {
      const std::string json = std::format(
          R"({{"format": 1, "keys": ["default"], "signals": {{ "s": {{ "actorState": "{}" }} }}}})",
          state.name);
      const Recipe r = StableRoundTrip(
          json, "state", std::format("actorState {}", state.name));
      const Signal *signal = r.FindSignal("s");
      const ActorStateSignal *read =
          signal ? Get<ActorStateSignal>(signal->kind) : nullptr;
      Check(read && read->kind == state.value,
            std::format("actorState {} parses to its kind", state.name));
    }
  }

  {
    const LoadResult unset = ParseRecipe(
        R"({"format":1,"keys":["default"],"outputs":[{"target":"material",)"
        R"("slot":"emissive","strength":1,"stack":[{"opacity":1}]}]})",
        "unset");
    Check(unset.recipe.has_value() && !unset.HasErrors(),
          "a layer without a source loads without an error");
    const auto *surface =
        unset.recipe && !unset.recipe->outputs.empty()
            ? std::get_if<SurfaceOutput>(&unset.recipe->outputs.front())
            : nullptr;
    Check(surface && surface->stack.size() == 1 &&
              IsUnset(surface->stack.front().source),
          "a layer without a source is unset");
    Check(std::ranges::any_of(
              unset.diagnostics,
              [](const Diagnostic &a_diagnostic) {
                return a_diagnostic.severity == Severity::kWarning &&
                       a_diagnostic.message.contains("no source yet");
              }),
          "an unset layer source is a warning");
    Check(unset.recipe &&
              !SerializeRecipe(*unset.recipe).contains("\"source\""),
          "an unset layer writes no source");
    if (unset.recipe) {
      const RecipeGraph graph = RecipeGraph::Compile(*unset.recipe);
      Check(graph.Diagnostics().empty(),
            "an unset layer lowers without a diagnostic");
    }
  }

  return test::Finish("recipe");
}
