#include "studio/Fields.h"
#include "test_support.h"

#include <array>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
template <class T>
[[nodiscard]] bool Holds(const std::optional<RecipeEdit> &a_edit) {
  return a_edit && Get<T>(*a_edit) != nullptr;
}
}

int main() {
  const std::optional<RecipeEdit> opacityEdit = BindLayerOpacity(2, 1)("0.5");
  const SetLayerOpacity *setOpacity =
      opacityEdit ? Get<SetLayerOpacity>(*opacityEdit) : nullptr;
  Check(setOpacity != nullptr && setOpacity->output == 2 &&
            setOpacity->layer == 1 &&
            Get<float>(setOpacity->opacity) != nullptr &&
            test::Near(*Get<float>(setOpacity->opacity), 0.5f),
        "BindLayerOpacity parses a number into SetLayerOpacity at its indices");
  Check(!BindLayerOpacity(0, 0)("not a number"),
        "a layer opacity that does not parse refuses the edit");

  const std::optional<RecipeEdit> colorEdit = BindLayerColor(0, 0)("1, 0, 0");
  const SetLayerColor *colorSet =
      colorEdit ? Get<SetLayerColor>(*colorEdit) : nullptr;
  Check(colorSet != nullptr && colorSet->color.has_value(),
        "BindLayerColor parses r,g,b into a set colour");
  const std::optional<RecipeEdit> clearEdit = BindLayerColor(0, 0)("");
  const SetLayerColor *colorClear =
      clearEdit ? Get<SetLayerColor>(*clearEdit) : nullptr;
  Check(colorClear != nullptr && !colorClear->color.has_value(),
        "an empty colour clears the layer colour rather than refusing");

  const std::optional<RecipeEdit> maskEdit = BindLayerMask(3, 0)("@grime");
  const SetLayerMask *maskSet =
      maskEdit ? Get<SetLayerMask>(*maskEdit) : nullptr;
  Check(maskSet != nullptr && maskSet->mask.has_value() &&
            maskSet->mask->name == "grime",
        "BindLayerMask strips the sigil into a mask reference");

  const std::optional<RecipeEdit> channelEdit = BindLayerChannels(0, 0)("rg");
  const SetLayerChannels *channels =
      channelEdit ? Get<SetLayerChannels>(*channelEdit) : nullptr;
  Check(channels != nullptr && channels->channels.r && channels->channels.g &&
            !channels->channels.b,
        "BindLayerChannels parses a channel set");

  const std::optional<RecipeEdit> strengthEdit =
      BindScalar(4, ScalarField::kStrength)("2");
  const SetScalar *strength =
      strengthEdit ? Get<SetScalar>(*strengthEdit) : nullptr;
  Check(strength != nullptr && strength->output == 4 &&
            strength->field == ScalarField::kStrength &&
            Get<float>(strength->value) != nullptr &&
            test::Near(*Get<float>(strength->value), 2.0f),
        "BindScalar routes a numeric field to SetScalar");
  const std::optional<RecipeEdit> colorScalarEdit =
      BindScalar(4, ScalarField::kColor)("1, 1, 1");
  Check(Holds<SetColorScalar>(colorScalarEdit),
        "BindScalar routes the colour field to SetColorScalar");

  const std::optional<RecipeEdit> lightColorEdit =
      BindLightVector(0, LightVector::kColor)("0.5, 0.5, 1");
  const SetLightVector *lightColor =
      lightColorEdit ? Get<SetLightVector>(*lightColorEdit) : nullptr;
  Check(lightColor != nullptr && lightColor->field == LightVector::kColor,
        "BindLightVector builds a light vector edit");
  const std::optional<RecipeEdit> shadowOnEdit = BindLightShadow(1)("on");
  const SetLightShadow *shadowOn =
      shadowOnEdit ? Get<SetLightShadow>(*shadowOnEdit) : nullptr;
  Check(shadowOn != nullptr && shadowOn->output == 1 && shadowOn->shadow,
        "BindLightShadow reads the on token");
  const std::optional<RecipeEdit> shadowOffEdit = BindLightShadow(1)("off");
  const SetLightShadow *shadowOff =
      shadowOffEdit ? Get<SetLightShadow>(*shadowOffEdit) : nullptr;
  Check(shadowOff != nullptr && !shadowOff->shadow,
        "BindLightShadow reads the off token");

  const std::optional<RecipeEdit> depthEdit = BindShellDepthBias()("on");
  const SetShellDepthBias *depth =
      depthEdit ? Get<SetShellDepthBias>(*depthEdit) : nullptr;
  Check(depth != nullptr && depth->on, "BindShellDepthBias reads the toggle");
  const std::optional<RecipeEdit> pointEdit =
      BindShellPoint(ShellPoint::kSpinAxis)("0, 0, 1");
  const SetShellPoint *point =
      pointEdit ? Get<SetShellPoint>(*pointEdit) : nullptr;
  Check(point != nullptr && point->field == ShellPoint::kSpinAxis &&
            test::Near(point->value.z, 1.0f),
        "BindShellPoint parses a literal vector, refusing signals");
  Check(!BindShellPoint(ShellPoint::kSpinAxis)("@spin"),
        "a shell point refuses a signal reference");

  const std::optional<RecipeEdit> materialEdit = BindShellMaterial()("pbrCopy");
  Check(Holds<SetShellMaterial>(materialEdit),
        "BindShellMaterial resolves a material word");

  const std::optional<RecipeEdit> signalKindEdit =
      BindSignalKind("glow")("pulse");
  const SetSignal *signalKind =
      signalKindEdit ? Get<SetSignal>(*signalKindEdit) : nullptr;
  Check(signalKind != nullptr && signalKind->signal == "glow" &&
            Get<PulseSignal>(signalKind->kind) != nullptr,
        "BindSignalKind swaps a signal to a default of the named kind");

  const SignalKind record = PulseSignal{};
  const FieldBinding base =
      BindSignalMember("glow", record, &PulseSignal::base, ParseParam);
  const std::optional<RecipeEdit> memberEdit = base("3");
  const SetSignal *memberSet =
      memberEdit ? Get<SetSignal>(*memberEdit) : nullptr;
  const PulseSignal *pulse =
      memberSet ? Get<PulseSignal>(memberSet->kind) : nullptr;
  Check(pulse != nullptr && Get<float>(pulse->base) != nullptr &&
            test::Near(*Get<float>(pulse->base), 3.0f),
        "BindSignalMember edits one member of the record, keeping the rest");

  {
    std::string owned = "myCurve";
    const FieldBinding curve = BindCurveText(owned);
    owned.clear();
    const std::optional<RecipeEdit> curveEdit = curve("x * 2");
    const SetCurve *setCurve = curveEdit ? Get<SetCurve>(*curveEdit) : nullptr;
    Check(setCurve != nullptr && setCurve->curve == "myCurve" &&
              setCurve->text == "x * 2",
          "BindCurveText captures the curve name by value, not by reference");
  }

  const FormField value =
      ValueField("opacity", FieldKind::kScalar, "0.5", {},
                 BindLayerOpacity(0, 0), std::nullopt, FieldDetail::kOpacity);
  Check(value.kind == FieldKind::kScalar && value.name == "opacity" &&
            value.detail == FieldDetail::kOpacity &&
            static_cast<bool>(value.bind),
        "ValueField finishes a bound scalar field with its detail");
  Check(kFieldKinds[static_cast<std::size_t>(FieldKind::kSignalValue)].check ==
            FieldCheckKind::kSignalValue,
        "a field carries its check as a value read from the kind table");

  const FormField reference =
      ReferenceField("mask", "@grime", {"grime"}, true, BindLayerMask(0, 0));
  Check(reference.kind == FieldKind::kReference && reference.allowEmpty &&
            reference.names.size() == 1,
        "ReferenceField builds a reference combo that allows the empty pick");

  const std::array<Blend, 3> allowed{Blend::kReplace, Blend::kMultiply,
                                     Blend::kAdd};
  const FormField blend =
      BlendField("blend", Blend::kMultiply, allowed, BindLayerBlend(0, 0));
  Check(blend.kind == FieldKind::kChoice && blend.text == "multiply" &&
            blend.names.size() == 3,
        "BlendField renders the allowed blends as a choice");

  const FormField toggle = ToggleField("shadow", true, BindLightShadow(0));
  Check(toggle.kind == FieldKind::kToggle && toggle.text == "on",
        "ToggleField spells the boolean as the on token");

  return test::Finish("studio_fields");
}
