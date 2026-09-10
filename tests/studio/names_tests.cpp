#include "studio/Names.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
void RowKindNames() {
  Check(RowKindName(RowKind::kSignal) == "signal" &&
            RowKindName(RowKind::kCurve) == "curve" &&
            RowKindName(RowKind::kSource) == "source" &&
            RowKindName(RowKind::kMask) == "mask",
        "each row kind names its domain word");
  Check(RowKindName(static_cast<RowKind>(kRowKindCount)) == "?",
        "an out-of-range kind names '?'");
}

[[nodiscard]] SignalRow Signal(std::string a_name, ValueType a_type) {
  SignalRow row;
  row.name = std::move(a_name);
  row.type = a_type;
  return row;
}

[[nodiscard]] TextRow Text(std::string a_name) {
  TextRow row;
  row.name = std::move(a_name);
  return row;
}

[[nodiscard]] PictureRow Picture(std::string a_name, ValueType a_type) {
  PictureRow row;
  row.name = std::move(a_name);
  row.type = a_type;
  return row;
}

void Collects() {
  RecipeRow recipe;
  recipe.signals.push_back(Signal("glow", ValueType::kScalar));
  recipe.signals.push_back(Signal("tint", ValueType::kVec3));
  recipe.curves.push_back(Text("ramp"));
  GeometryRow geometry;
  geometry.sources.push_back(Picture("metal", ValueType::kVec3));
  geometry.masks.push_back(Picture("edge", ValueType::kScalar));

  const Names names = NamesOf(recipe, geometry);
  Check(names.signals.size() == 2 && names.signals[0].first == "glow" &&
            names.signals[0].second == ValueType::kScalar &&
            names.signals[1].second == ValueType::kVec3,
        "signals carry name and value type");
  Check(names.curves.size() == 1 && names.curves[0] == "ramp",
        "curves carry names");
  Check(names.sources.size() == 1 && names.sources[0].first == "metal" &&
            names.sources[0].second == ValueType::kVec3,
        "sources carry name and value type");
  Check(names.masks.size() == 1 && names.masks[0] == "edge",
        "masks carry names");

  Check(TakenNames(RowKind::kSignal, names) ==
            std::vector<std::string>{"glow", "tint"},
        "signal taken names are the signals");
  Check(TakenNames(RowKind::kCurve, names) == std::vector<std::string>{"ramp"},
        "curve taken names are the curves");
  const std::vector<std::string> texel{"metal", "edge"};
  Check(TakenNames(RowKind::kSource, names) == texel &&
            TakenNames(RowKind::kMask, names) == texel,
        "source and mask share the sources-then-masks name space");
}

void UniqueNames() {
  const std::vector<std::string> taken{"glow", "signal", "signal2"};
  Check(UniqueName("glow2", taken) == "glow2", "a free stem is the name");
  Check(UniqueName("signal", taken) == "signal3",
        "a taken stem takes the first free number from 2");
  Check(UniqueName("curve", taken) == "curve", "an untaken stem is unchanged");
  Check(UniqueName("x", {}) == "x", "nothing taken");
  const std::vector<std::string> full{"x", "x2", "x3"};
  Check(UniqueName("x", full) == "x4", "the number counts up past clashes");
}

void References() {
  Check(ReferenceText("metal") == "@metal", "reference text adds the sigil");
  Check(ReferenceName("@metal") == "metal" && ReferenceName("metal") == "metal",
        "reference name strips one sigil, tolerates none");
  Check(ReferenceName("@@x") == "@x", "only one sigil is stripped");
  Check(ReferenceName("") == "" && ReferenceName("@") == "" &&
            ReferenceText("") == "@",
        "empty texts");
}

void Matches() {
  Check(NameMatches("glowHue", "") && NameMatches("", ""),
        "an empty filter passes every name");
  Check(NameMatches("glowHue", "hue") && NameMatches("glowHue", "GLOW") &&
            NameMatches("glowHue", "glowHue"),
        "a filter matches anywhere, case ignored");
  Check(!NameMatches("glowHue", "ring") && !NameMatches("", "a") &&
            !NameMatches("hue", "glowHue"),
        "a name without the filter fails");
}

void GeometryLabels() {
  Check(GeometryLabel("Armor003", "Iron Cuirass") == "Armor003",
        "an authored geometry keeps its name");
  Check(GeometryLabel(" (FE034935)[0]/ (2500097A) [100%]",
                      "Northern Iron Boots") ==
            "Northern Iron Boots geometry 0 (addon FE034935)",
        "an engine-built geometry reads as armor, index and addon");
  Check(GeometryLabel(" (0008E840)[12]/ (00100E29) [50%]", "") ==
            "armor 00100E29 geometry 12 (addon 0008E840)",
        "no armor name falls back to the armor id");
  Check(GeometryLabel(" (FE03493)[0]/ (2500097A) [100%]", "Boots") ==
            " (FE03493)[0]/ (2500097A) [100%]",
        "a short id is not the pattern");
  Check(GeometryLabel(" (FE034935)[x]/ (2500097A) [100%]", "Boots") ==
            " (FE034935)[x]/ (2500097A) [100%]",
        "a non-numeric index is not the pattern");
  Check(GeometryLabel(" (FE034935)[0]", "Boots") == " (FE034935)[0]",
        "a truncated name is not the pattern");
  Check(GeometryLabel("", "Boots").empty(), "an empty name stays empty");
}
}

int main() {
  RowKindNames();
  Collects();
  UniqueNames();
  References();
  Matches();
  GeometryLabels();
  return test::Finish("studio_names");
}
