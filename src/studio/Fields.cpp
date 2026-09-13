#include "studio/Fields.h"

#include "studio/Names.h"
#include "studio/Page.h"

#include <array>
#include <charconv>
#include <string_view>
#include <system_error>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::optional<Vec3> LiteralVec3(const std::string &a_text) {
  const auto parsed = ParseVec3Param(a_text);
  if (!parsed) {
    return std::nullopt;
  }
  const auto *parts = Get<std::array<Param, 3>>(*parsed);
  if (parts == nullptr) {
    return std::nullopt;
  }
  Vec3 out;
  std::array<float *, 3> slots{&out.x, &out.y, &out.z};
  for (std::size_t i = 0; i < 3; ++i) {
    const float *number = Get<float>((*parts)[i]);
    if (number == nullptr) {
      return std::nullopt;
    }
    *slots[i] = *number;
  }
  return out;
}

[[nodiscard]] std::optional<int> WholeInt(std::string_view a_text) {
  while (!a_text.empty() && a_text.front() == ' ') {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() && a_text.back() == ' ') {
    a_text.remove_suffix(1);
  }
  int value = 0;
  const auto result =
      std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
  if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] bool BlankText(std::string_view a_text) noexcept {
  return a_text.find_first_not_of(' ') == std::string_view::npos;
}
}

FieldBinding BindLayerSource(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto source = ParseLayerSource(a_text);
    if (!source) {
      return std::nullopt;
    }
    return SetLayerSource{a_output, a_layer, *source};
  };
}

FieldBinding BindLayerCurve(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    std::optional<CurveRef> curve;
    if (!a_text.empty()) {
      curve = CurveRef{a_text};
    }
    return SetLayerCurve{a_output, a_layer, curve};
  };
}

FieldBinding BindLayerOpacity(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto opacity = ParseParam(a_text);
    if (!opacity) {
      return std::nullopt;
    }
    return SetLayerOpacity{a_output, a_layer, *opacity};
  };
}

FieldBinding BindLayerColor(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    if (a_text.empty()) {
      return SetLayerColor{a_output, a_layer, std::nullopt};
    }
    const auto color = ParseColorParam(a_text);
    if (!color) {
      return std::nullopt;
    }
    return SetLayerColor{a_output, a_layer, *color};
  };
}

FieldBinding BindLayerMask(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto name = ReferenceName(a_text);
    std::optional<Ref> mask;
    if (!name.empty()) {
      mask = Ref{name};
    }
    return SetLayerMask{a_output, a_layer, mask};
  };
}

FieldBinding BindLayerChannels(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto channels = ChannelSet::Parse(a_text);
    if (!channels) {
      return std::nullopt;
    }
    return SetLayerChannels{a_output, a_layer, *channels};
  };
}

FieldBinding BindLayerBlend(std::size_t a_output, std::size_t a_layer) {
  return [a_output,
          a_layer](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto blend = ParseBlend(a_text);
    if (!blend) {
      return std::nullopt;
    }
    return SetLayerBlend{a_output, a_layer, *blend};
  };
}

FieldBinding BindScalar(std::size_t a_output,
                        std::optional<ScalarField> a_field) {
  return [a_output,
          a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    if (!a_field) {
      return std::nullopt;
    }
    if (*a_field == ScalarField::kColor) {
      const auto color = ParseColorParam(a_text);
      if (!color) {
        return std::nullopt;
      }
      return SetColorScalar{a_output, *color};
    }
    const auto value = ParseParam(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetScalar{a_output, *a_field, *value};
  };
}

FieldBinding BindLightParam(std::size_t a_output, LightParam a_field) {
  return [a_output,
          a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = ParseParam(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetLightParam{a_output, a_field, *value};
  };
}

FieldBinding BindLightVector(std::size_t a_output, LightVector a_field) {
  return [a_output,
          a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = a_field == LightVector::kColor ? ParseColorParam(a_text)
                                                      : ParseVec3Param(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetLightVector{a_output, a_field, *value};
  };
}

FieldBinding BindLightShadow(std::size_t a_output) {
  return [a_output](const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetLightShadow{a_output, a_text == "on"};
  };
}

FieldBinding BindLightReplace(std::size_t a_output) {
  return [a_output](const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetLightReplace{a_output, a_text == "on"};
  };
}

FieldBinding BindShellParam(ShellParam a_field) {
  return [a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = ParseParam(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetShellParam{a_field, *value};
  };
}

FieldBinding BindShellVector(ShellVector a_field) {
  return [a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = ParseVec3Param(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetShellVector{a_field, *value};
  };
}

FieldBinding BindShellPoint(ShellPoint a_field) {
  return [a_field](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = LiteralVec3(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetShellPoint{a_field, *value};
  };
}

FieldBinding BindShellMaterial() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto material = ParseShellMaterial(a_text);
    if (!material) {
      return std::nullopt;
    }
    return SetShellMaterial{*material};
  };
}

FieldBinding BindShellBlend() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto blend = ParseShellBlend(a_text);
    if (!blend) {
      return std::nullopt;
    }
    return SetShellBlend{*blend};
  };
}

FieldBinding BindShellDepthBias() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetShellDepthBias{a_text == "on"};
  };
}

FieldBinding BindShellAlphaTest() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = ParseParam(a_text);
    const float *number = value ? Get<float>(*value) : nullptr;
    if (number == nullptr) {
      return std::nullopt;
    }
    return SetShellAlphaTest{*number};
  };
}

FieldBinding BindPriority() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    if (BlankText(a_text)) {
      return SetPriority{std::nullopt};
    }
    const auto value = WholeInt(a_text);
    if (!value) {
      return std::nullopt;
    }
    return SetPriority{*value};
  };
}

FieldBinding BindClockSpeed() {
  return [](const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto value = ParseParam(a_text);
    const float *number = value ? Get<float>(*value) : nullptr;
    if (number == nullptr) {
      return std::nullopt;
    }
    return SetClockSpeed{*number};
  };
}

FieldBinding BindOutputReplace(std::size_t a_output) {
  return [a_output](const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetOutputReplace{a_output, a_text == "on"};
  };
}

FieldBinding BindSignalKind(std::string a_signal) {
  return [signal = std::move(a_signal)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto kind = DefaultSignalKind(a_text);
    if (!kind) {
      return std::nullopt;
    }
    return SetSignal{signal, *kind};
  };
}

FieldBinding BindCurveText(std::string a_curve) {
  return [curve = std::move(a_curve)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetCurve{curve, a_text};
  };
}

FieldBinding BindMaskText(std::string a_mask) {
  return [mask = std::move(a_mask)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    return SetMask{mask, a_text};
  };
}

FormField ValueField(ValueFieldSpec a_spec) {
  FormField field;
  field.name = std::move(a_spec.name);
  field.kind = a_spec.kind;
  field.text = std::move(a_spec.text);
  field.names = std::move(a_spec.names);
  field.allowEmpty = a_spec.allowEmpty;
  field.detail = a_spec.detail;
  field.value = a_spec.value;
  field.bind = std::move(a_spec.bind);
  field.workingRange = a_spec.workingRange;
  field.units = std::move(a_spec.units);
  field.integral = a_spec.integral;
  return field;
}

FormField ReferenceField(ReferenceFieldSpec a_spec) {
  FormField field;
  field.name = std::move(a_spec.name);
  field.kind = FieldKind::kReference;
  field.text = std::move(a_spec.text);
  field.names = std::move(a_spec.names);
  field.allowEmpty = a_spec.allowEmpty;
  field.detail = a_spec.detail;
  field.bind = std::move(a_spec.bind);
  field.creators = std::move(a_spec.creators);
  field.create = std::move(a_spec.create);
  return field;
}

FormField ChoiceField(std::string a_name, std::string a_current,
                      std::vector<std::string> a_choices, FieldBinding a_bind) {
  FormField field;
  field.name = std::move(a_name);
  field.kind = FieldKind::kChoice;
  field.text = std::move(a_current);
  field.names = std::move(a_choices);
  field.bind = std::move(a_bind);
  return field;
}

FormField BlendField(std::string a_name, Blend a_current,
                     std::span<const Blend> a_allowed, FieldBinding a_bind) {
  FormField field;
  field.name = std::move(a_name);
  field.kind = FieldKind::kChoice;
  field.text = std::string{BlendName(a_current)};
  field.names.reserve(a_allowed.size());
  for (const Blend blend : a_allowed) {
    field.names.emplace_back(BlendName(blend));
  }
  field.bind = std::move(a_bind);
  return field;
}

FormField ToggleField(std::string a_name, bool a_on, FieldBinding a_bind) {
  FormField field;
  field.name = std::move(a_name);
  field.kind = FieldKind::kToggle;
  field.text = a_on ? "on" : "off";
  field.bind = std::move(a_bind);
  return field;
}

FormField TextedField(TextedFieldSpec a_spec) {
  FormField field;
  field.name = std::move(a_spec.name);
  field.kind = a_spec.kind;
  field.text = std::move(a_spec.text);
  field.allowEmpty = a_spec.allowEmpty;
  field.bind = std::move(a_spec.bind);
  return field;
}

FormID ActorOf(const Page &a_page) noexcept {
  return a_page.piece != nullptr ? a_page.piece->ref.actorID : FormID{0};
}

std::span<const BoneCoverage> BonesOf(const Page &a_page) noexcept {
  if (a_page.geometry == nullptr) {
    return {};
  }
  return a_page.geometry->bones;
}

float ScaleOf(const Page &a_page) noexcept { return a_page.scale; }
}
