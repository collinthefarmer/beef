// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/FieldCheck.h"

#include "Core.h"
#include "studio/Forms.h"

#include "test_support.h"

#include <string>
#include <utility>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] Names SampleNames() {
  Names names;
  names.signals.emplace_back("glow", ValueType::kScalar);
  names.signals.emplace_back("tint", ValueType::kVec3);
  names.curves.emplace_back("ramp");
  names.sources.emplace_back("diffuse", ValueType::kScalar);
  names.masks.emplace_back("edge");
  return names;
}
}

int main() {
  const Names names = SampleNames();

  Check(!CheckSignalValue("1.0", names).has_value(),
        "a bare number is a valid signal value");
  Check(ProblemText(CheckSignalValue("", names)) == "cannot be empty",
        "an empty signal value is refused");
  Check(!CheckSignalValue("@glow * 2", names).has_value(),
        "an expression over a known scalar signal is a valid signal value");
  Check(ProblemText(CheckSignalValue("@missing", names)) ==
            "'@missing' is not a signal",
        "an unknown reference in a signal value names the missing signal");

  Check(!CheckCurveText("x * 2", names).has_value(),
        "x is defined inside a curve");
  Check(ProblemText(CheckCurveText("", names)) == "cannot be empty",
        "an empty curve is refused");
  Check(CheckCurveText("@glow +", names).has_value(),
        "a malformed curve expression is caught");

  Check(!CheckMaskText("@diffuse", names).has_value(),
        "a source name is a valid mask texel reference");
  Check(ProblemText(CheckMaskText("@missing", names)) ==
            "'@missing' is not a source, mask or signal",
        "an unknown mask reference names sources, masks and signals");
  Check(ProblemText(CheckMaskText("", names)) == "cannot be empty",
        "an empty mask is refused");

  FormField scalar;
  scalar.kind = FieldKind::kScalar;
  scalar.range = std::pair<float, float>{0.0f, 1.0f};
  Check(!CheckField(scalar, "0.5", names).has_value(),
        "an in-range scalar field passes");
  Check(CheckField(scalar, "5", names).has_value(),
        "an out-of-range scalar field fails with a bound hint");
  Check(ProblemText(CheckField(scalar, "not a number", names)) ==
            "a number, or @signal",
        "an unparseable scalar field reports the expected shape");

  FormField colour;
  colour.kind = FieldKind::kColor;
  colour.names = {"tint"};
  Check(!CheckField(colour, "@tint", names).has_value(),
        "a colour field accepts a listed reference");
  Check(ProblemText(CheckField(colour, "@missing", names)) ==
            "'@missing' is not one of the rows this field takes",
        "a colour field rejects an unlisted reference by name");

  FormField signalValue;
  signalValue.kind = FieldKind::kSignalValue;
  Check(ProblemText(CheckField(signalValue, "x + 1", names)) ==
            "'x' is only defined inside a curve",
        "x outside a curve is rejected in a signal-value field");

  FormField name;
  name.kind = FieldKind::kName;
  name.text = "glow";
  name.names = {"glow", "tint"};
  Check(!CheckField(name, "glow", names).has_value(),
        "a name field accepts the row's own current name");
  Check(ProblemText(CheckField(name, "tint", names)) ==
            "another row is named 'tint'",
        "a name field rejects another row's name");
  Check(ProblemText(CheckField(name, "1bad", names)) ==
            "letters, digits and underscores, not starting with a digit",
        "a name field rejects an identifier starting with a digit");

  FormField channels;
  channels.kind = FieldKind::kChannels;
  Check(!CheckField(channels, "rgb", names).has_value(),
        "a channels field accepts r g b a in any order");
  Check(ProblemText(CheckField(channels, "xyz", names)) == "any of r g b a",
        "a channels field rejects non-channel letters");

  return test::Finish("studio_fieldcheck");
}
