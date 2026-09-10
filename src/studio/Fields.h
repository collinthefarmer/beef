#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Forms.h"

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] FieldBinding BindLayerSource(std::size_t a_output,
                                           std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerCurve(std::size_t a_output,
                                          std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerOpacity(std::size_t a_output,
                                            std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerColor(std::size_t a_output,
                                          std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerMask(std::size_t a_output,
                                         std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerChannels(std::size_t a_output,
                                             std::size_t a_layer);
[[nodiscard]] FieldBinding BindLayerBlend(std::size_t a_output,
                                          std::size_t a_layer);

[[nodiscard]] FieldBinding BindScalar(std::size_t a_output,
                                      std::optional<ScalarField> a_field);

[[nodiscard]] FieldBinding BindLightParam(std::size_t a_output,
                                          LightParam a_field);
[[nodiscard]] FieldBinding BindLightVector(std::size_t a_output,
                                           LightVector a_field);
[[nodiscard]] FieldBinding BindLightShadow(std::size_t a_output);
[[nodiscard]] FieldBinding BindLightReplace(std::size_t a_output);

[[nodiscard]] FieldBinding BindShellParam(ShellParam a_field);
[[nodiscard]] FieldBinding BindShellVector(ShellVector a_field);
[[nodiscard]] FieldBinding BindShellPoint(ShellPoint a_field);
[[nodiscard]] FieldBinding BindShellMaterial();
[[nodiscard]] FieldBinding BindShellBlend();
[[nodiscard]] FieldBinding BindShellDepthBias();
[[nodiscard]] FieldBinding BindShellAlphaTest();

[[nodiscard]] FieldBinding BindSignalKind(std::string a_signal);
[[nodiscard]] FieldBinding BindCurveText(std::string a_curve);
[[nodiscard]] FieldBinding BindMaskText(std::string a_mask);

template <class S, class M, class Parse>
[[nodiscard]] FieldBinding BindSignalMember(std::string a_signal,
                                            SignalKind a_record, M S::*a_member,
                                            Parse a_parse) {
  return [signal = std::move(a_signal), record = std::move(a_record), a_member,
          a_parse](const std::string &a_text) -> std::optional<RecipeEdit> {
    SignalKind kind = record;
    S *held = Get<S>(kind);
    if (held == nullptr) {
      return std::nullopt;
    }
    const auto value = a_parse(a_text);
    if (!value) {
      return std::nullopt;
    }
    held->*a_member = *value;
    return SetSignal{signal, kind};
  };
}

template <class S, class M, class Parse>
[[nodiscard]] FieldBinding BindSourceMember(std::string a_source,
                                            SourceKind a_record, M S::*a_member,
                                            Parse a_parse) {
  return [source = std::move(a_source), record = std::move(a_record), a_member,
          a_parse](const std::string &a_text) -> std::optional<RecipeEdit> {
    SourceKind kind = record;
    S *held = Get<S>(kind);
    if (held == nullptr) {
      return std::nullopt;
    }
    const auto value = a_parse(a_text);
    if (!value) {
      return std::nullopt;
    }
    held->*a_member = *value;
    return SetSource{source, kind};
  };
}

[[nodiscard]] FormField
ValueField(std::string a_name, FieldKind a_kind, std::string a_text,
           std::vector<std::string> a_names, FieldBinding a_bind,
           std::optional<Value> a_value = std::nullopt,
           std::optional<FieldDetail> a_detail = std::nullopt,
           bool a_allowEmpty = false);
[[nodiscard]] FormField ReferenceField(
    std::string a_name, std::string a_text, std::vector<std::string> a_names,
    bool a_allowEmpty, FieldBinding a_bind,
    std::optional<FieldDetail> a_detail = std::nullopt,
    std::vector<std::string> a_creators = {}, FieldCreator a_create = {});
[[nodiscard]] FormField ChoiceField(std::string a_name, std::string a_current,
                                    std::vector<std::string> a_choices,
                                    FieldBinding a_bind);
[[nodiscard]] FormField BlendField(std::string a_name, Blend a_current,
                                   std::span<const Blend> a_allowed,
                                   FieldBinding a_bind);
[[nodiscard]] FormField ToggleField(std::string a_name, bool a_on,
                                    FieldBinding a_bind);
[[nodiscard]] FormField TextedField(std::string a_name, FieldKind a_kind,
                                    std::string a_text, FieldBinding a_bind,
                                    bool a_allowEmpty = false);
}
