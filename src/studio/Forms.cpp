#include "studio/Forms.h"

#include "recipe/Expression.h"
#include "recipe/Words.h"
#include "studio/FieldParsing.h"
#include "studio/Fields.h"
#include "studio/Rows.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool NamesSignal(const Inspector &a_inspector,
                               const std::string &a_text) {
  return a_text.starts_with('@') &&
         FindByName(a_inspector.signals, ReferenceName(a_text)) != nullptr;
}

constexpr std::pair<float, float> kUnitRange{0.0f, 1.0f};
constexpr std::pair<float, float> kLightIntensityRange{0.0f, 16.0f};
constexpr std::pair<float, float> kLightSizeRange{0.0f, 8.0f};
constexpr std::pair<float, float> kLightCutoffRange{0.01f, 1.0f};
constexpr std::pair<float, float> kShellEmissiveRange{0.0f, 10.0f};
constexpr std::pair<float, float> kShellRimPowerRange{0.0f, 8.0f};
constexpr std::pair<float, float> kShellScaleRange{0.0f, 3.0f};
constexpr std::pair<float, float> kImageMipRange{0.0f, 12.0f};

[[nodiscard]] std::optional<FieldDetail> DetailWhen(bool a_present,
                                                    FieldDetail a_detail) {
  return a_present ? std::optional{a_detail} : std::nullopt;
}

[[nodiscard]] std::vector<std::string>
Joined(std::span<const std::string> a_a, std::span<const std::string> a_b) {
  std::vector<std::string> out(a_a.begin(), a_a.end());
  out.insert(out.end(), a_b.begin(), a_b.end());
  return out;
}

[[nodiscard]] std::optional<std::uint32_t> ParseCount(std::string_view a_text) {
  const auto count = WholeNumber(a_text, 64);
  return count && *count >= 1 ? count : std::nullopt;
}

[[nodiscard]] std::vector<std::string> BipedSlotNames() {
  std::vector<std::string> names;
  for (std::uint32_t slot = std::to_underlying(kFirstBipedSlot);
       slot <= std::to_underlying(kLastBipedSlot); ++slot) {
    if (const auto name = BipedSlotName(BipedSlot{slot})) {
      names.emplace_back(*name);
    }
  }
  return names;
}

[[nodiscard]] std::optional<Ref> RefOf(const std::string &a_text) {
  return IsWholeReference(a_text) ? std::optional{Ref{ReferenceName(a_text)}}
                                  : std::nullopt;
}

[[nodiscard]] std::optional<std::optional<Ref>>
OptionalRefOf(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<Ref>{};
  }
  const auto ref = RefOf(a_text);
  if (!ref) {
    return std::nullopt;
  }
  return std::optional<Ref>{*ref};
}

[[nodiscard]] std::optional<std::optional<Param>>
OptionalParamOf(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<Param>{};
  }
  const auto param = ParseParam(a_text);
  if (!param) {
    return std::nullopt;
  }
  return std::optional<Param>{*param};
}

[[nodiscard]] std::optional<std::optional<Vec2Param>>
OptionalVec2Of(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<Vec2Param>{};
  }
  const auto pair = ParseVec2Param(a_text);
  if (!pair) {
    return std::nullopt;
  }
  return std::optional<Vec2Param>{*pair};
}

[[nodiscard]] std::optional<std::string> TextOf(const std::string &a_text) {
  return a_text.empty() ? std::nullopt : std::optional{a_text};
}

[[nodiscard]] std::optional<std::string>
AlwaysString(const std::string &a_text) {
  return std::optional{a_text};
}

[[nodiscard]] std::optional<bool> AlwaysOnOff(const std::string &a_text) {
  return std::optional{a_text == "on"};
}

[[nodiscard]] std::optional<float> NumberOf(const std::string &a_text) {
  const auto param = ParseParam(a_text);
  const float *number = param ? Get<float>(*param) : nullptr;
  return number != nullptr ? std::optional{*number} : std::nullopt;
}

[[nodiscard]] std::optional<std::optional<float>>
OptionalNumberOf(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<float>{};
  }
  const auto number = NumberOf(a_text);
  if (!number) {
    return std::nullopt;
  }
  return std::optional<float>{*number};
}

[[nodiscard]] std::string BoundText(const std::optional<float> &a_bound) {
  return a_bound ? std::format("{:g}", *a_bound) : std::string{};
}

[[nodiscard]] std::optional<FormRef> FormOf(const std::string &a_text) {
  return a_text.empty() ? std::nullopt : std::optional{FormRef::From(a_text)};
}

[[nodiscard]] std::optional<std::string>
DistanceFromOf(const std::string &a_text) {
  return a_text.empty() ? std::nullopt : std::optional{a_text};
}

[[nodiscard]] std::optional<std::uint8_t>
ClusterCountOf(const std::string &a_text) {
  const auto count = WholeNumber(a_text, kMaxMaterialClusters);
  return count && *count >= 1 ? std::optional{static_cast<std::uint8_t>(*count)}
                              : std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> SeedOf(const std::string &a_text) {
  return WholeNumber(a_text, std::numeric_limits<std::uint32_t>::max());
}

[[nodiscard]] std::optional<std::uint32_t>
IterationsOf(const std::string &a_text) {
  const auto count = WholeNumber(a_text, kMaxClusterIterations);
  return count && *count >= 1 ? count : std::nullopt;
}

template <class Row, std::size_t N>
[[nodiscard]] auto WordOf(const Row (&a_table)[N]) {
  return [&a_table](const std::string &a_text) {
    return FromName(a_table, a_text);
  };
}

[[nodiscard]] std::string RefText(const Ref &a_ref) {
  return ReferenceText(a_ref.name);
}

[[nodiscard]] std::string RefText(const std::optional<Ref> &a_ref) {
  return a_ref ? ReferenceText(a_ref->name) : std::string{};
}

[[nodiscard]] std::string ParamTextOf(const std::optional<Param> &a_param) {
  return a_param ? ParamText(*a_param) : std::string{};
}

constexpr const char *kImageCreators[]{
    "new image",    "new material", "new bake", "new uv",
    "new distance", "new ripple",   "new mask"};
constexpr const char *kValueCreators[]{"promote to signal", "new constant",
                                       "new expression"};

[[nodiscard]] std::vector<std::string>
Creators(std::span<const char *const> a_names) {
  return std::vector<std::string>(a_names.begin(), a_names.end());
}

[[nodiscard]] Created WireInto(Created a_made, const FieldBinding &a_bind) {
  if (const auto name = ResourceName(a_made.subject)) {
    if (const auto bound = a_bind(ReferenceText(*name))) {
      a_made.edits.push_back(*bound);
    }
  }
  return a_made;
}

[[nodiscard]] std::optional<Created> CreateImage(const std::string &a_creator,
                                                 const RecipeRow &a_recipe,
                                                 const FieldBinding &a_bind) {
  if (a_creator == "new mask") {
    return WireInto(Create(NewMask{}, a_recipe), a_bind);
  }
  const std::string_view word = a_creator.starts_with("new ")
                                    ? std::string_view{a_creator}.substr(4)
                                    : std::string_view{a_creator};
  if (const auto kind = DefaultSourceKind(word)) {
    return WireInto(Create(NewSource{std::string{word}, *kind}, a_recipe),
                    a_bind);
  }
  return std::nullopt;
}

struct CreateValueSpec {
  const std::string &creator;
  std::string_view field;
  const std::string &current;
  bool colour;
  const RecipeRow &recipe;
  const FieldBinding &bind;
};

[[nodiscard]] std::optional<Value> LiteralValue(const std::string &a_text,
                                                bool a_colour) {
  if (a_colour) {
    const auto colour = LiteralColor(a_text);
    return colour ? std::optional<Value>{*colour} : std::nullopt;
  }
  const auto param = ParseParam(a_text);
  const float *number = param ? Get<float>(*param) : nullptr;
  return number ? std::optional<Value>{*number} : std::nullopt;
}

[[nodiscard]] std::optional<Created>
CreateValue(const CreateValueSpec &a_spec) {
  if (a_spec.creator == "promote to signal") {
    const auto value = LiteralValue(a_spec.current, a_spec.colour);
    if (!value) {
      return std::nullopt;
    }
    Created made = Create(NewSignal{std::string{a_spec.field}}, a_spec.recipe);
    if (const auto name = ResourceName(made.subject)) {
      made.edits.emplace_back(SetConstant{*name, *value});
    }
    return WireInto(std::move(made), a_spec.bind);
  }
  if (a_spec.creator == "new constant") {
    Created made = Create(NewSignal{}, a_spec.recipe);
    const auto name = a_spec.colour ? ResourceName(made.subject) : std::nullopt;
    if (name) {
      made.edits.emplace_back(SetConstant{*name, Vec3{1.0f, 1.0f, 1.0f}});
    }
    return WireInto(std::move(made), a_spec.bind);
  }
  if (a_spec.creator == "new expression") {
    Created made = Create(NewSignal{}, a_spec.recipe);
    if (const auto name = ResourceName(made.subject)) {
      made.edits.emplace_back(
          SetExpression{*name, a_spec.colour ? "[1, 1, 1]" : "1"});
    }
    return WireInto(std::move(made), a_spec.bind);
  }
  return std::nullopt;
}

struct ParamFieldSpec {
  std::string name;
  FieldKind kind;
  std::string text;
  std::vector<std::string> names;
  FieldBinding bind;
  std::optional<std::pair<float, float>> workingRange = std::nullopt;
  std::string units = "";
  bool integral = false;
};

[[nodiscard]] FormField ParamField(ParamFieldSpec a_spec) {
  const bool valued =
      a_spec.kind == FieldKind::kScalar || a_spec.kind == FieldKind::kColor ||
      a_spec.kind == FieldKind::kVector || a_spec.kind == FieldKind::kVec2;
  const bool signal =
      valued && IsWholeReference(a_spec.text) &&
      std::ranges::contains(a_spec.names, ReferenceName(a_spec.text));
  const bool embedsReferences = valued && !IsWholeReference(a_spec.text) &&
                                a_spec.text.find('@') != std::string::npos;
  const bool offers =
      !a_spec.names.empty() &&
      (a_spec.kind == FieldKind::kScalar || a_spec.kind == FieldKind::kColor);
  FormField field = ValueField(
      {.name = a_spec.name,
       .kind = a_spec.kind,
       .text = a_spec.text,
       .names = a_spec.names,
       .bind = std::move(a_spec.bind),
       .detail =
           signal ? std::optional{FieldDetail::kSignal}
                  : (embedsReferences ? std::optional{FieldDetail::kReferences}
                                      : std::nullopt),
       .workingRange = a_spec.workingRange,
       .units = std::move(a_spec.units),
       .integral = a_spec.integral});
  if (offers) {
    field.creators = Creators(kValueCreators);
    field.create = [name = a_spec.name, current = a_spec.text,
                    colour = a_spec.kind == FieldKind::kColor,
                    bind = field.bind](const std::string &a_creator,
                                       const RecipeRow &a_recipe) {
      return CreateValue({.creator = a_creator,
                          .field = name,
                          .current = current,
                          .colour = colour,
                          .recipe = a_recipe,
                          .bind = bind});
    };
  }
  return field;
}

[[nodiscard]] FieldBinding BindSignalValue(std::string a_signal) {
  return [signal = std::move(a_signal)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    return SignalValueEdit(signal, a_text);
  };
}

[[nodiscard]] FieldBinding BindTriggerOrigin(std::string a_signal,
                                             SignalKind a_record) {
  return [signal = std::move(a_signal), record = std::move(a_record)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    for (std::size_t i = 0; i < std::size(kTriggerOriginWords); ++i) {
      if (kTriggerOriginWords[i] != a_text) {
        continue;
      }
      SignalKind kind = record;
      TriggerSignal *trigger = Get<TriggerSignal>(kind);
      if (trigger == nullptr) {
        return std::nullopt;
      }
      const auto made = AlternativeAt<TriggerOrigin>(i);
      if (!made) {
        return std::nullopt;
      }
      trigger->origin = *made;
      return SetSignal{signal, kind};
    }
    return std::nullopt;
  };
}

template <class O, class M, class Parse>
[[nodiscard]] FieldBinding BindTriggerMember(std::string a_signal,
                                             SignalKind a_record,
                                             M O::*a_member, Parse a_parse) {
  return [signal = std::move(a_signal), record = std::move(a_record), a_member,
          a_parse](const std::string &a_text) -> std::optional<RecipeEdit> {
    SignalKind kind = record;
    TriggerSignal *trigger = Get<TriggerSignal>(kind);
    O *origin = trigger != nullptr ? Get<O>(trigger->origin) : nullptr;
    if (origin == nullptr) {
      return std::nullopt;
    }
    const auto value = a_parse(a_text);
    if (!value) {
      return std::nullopt;
    }
    origin->*a_member = *value;
    return SetSignal{signal, kind};
  };
}

template <class M, class Parse>
[[nodiscard]] FieldBinding
BindEventFilter(std::string a_signal, SignalKind a_record,
                M EventFilter::*a_member, Parse a_parse) {
  return [signal = std::move(a_signal), record = std::move(a_record), a_member,
          a_parse](const std::string &a_text) -> std::optional<RecipeEdit> {
    SignalKind kind = record;
    TriggerSignal *trigger = Get<TriggerSignal>(kind);
    EventOrigin *origin =
        trigger != nullptr ? Get<EventOrigin>(trigger->origin) : nullptr;
    if (origin == nullptr) {
      return std::nullopt;
    }
    const auto value = a_parse(a_text);
    if (!value) {
      return std::nullopt;
    }
    origin->filter.*a_member = *value;
    return SetSignal{signal, kind};
  };
}

[[nodiscard]] FieldBinding
BindEventFilterBound(std::string a_signal, SignalKind a_record,
                     std::optional<float> ValueRange::*a_member) {
  return [signal = std::move(a_signal), record = std::move(a_record),
          a_member](const std::string &a_text) -> std::optional<RecipeEdit> {
    SignalKind kind = record;
    TriggerSignal *trigger = Get<TriggerSignal>(kind);
    EventOrigin *origin =
        trigger != nullptr ? Get<EventOrigin>(trigger->origin) : nullptr;
    if (origin == nullptr) {
      return std::nullopt;
    }
    const auto bound = OptionalNumberOf(a_text);
    if (!bound) {
      return std::nullopt;
    }
    origin->filter.value.*a_member = *bound;
    return SetSignal{signal, kind};
  };
}

[[nodiscard]] FieldBinding BindSourceKindChoice(std::string a_name) {
  return [name = std::move(a_name)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto kind = DefaultSourceKind(a_text);
    return kind ? std::optional<RecipeEdit>{SetSource{name, *kind}}
                : std::nullopt;
  };
}

[[nodiscard]] FieldBinding
BindImageMirror(std::string a_name, SourceKind a_record, std::size_t a_axis) {
  return [name = std::move(a_name), record = std::move(a_record),
          a_axis](const std::string &a_text) -> std::optional<RecipeEdit> {
    SourceKind kind = record;
    ImageSource *image = Get<ImageSource>(kind);
    if (image == nullptr || a_axis >= image->mirror.size()) {
      return std::nullopt;
    }
    image->mirror[a_axis] = a_text == "on";
    return SetSource{name, kind};
  };
}

[[nodiscard]] FieldBinding BindBakePartition(std::string a_name,
                                             SourceKind a_record) {
  return [name = std::move(a_name), record = std::move(a_record)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    SourceKind kind = record;
    BakeSource *bake = Get<BakeSource>(kind);
    PartitionBake *partition =
        bake != nullptr ? Get<PartitionBake>(bake->bake) : nullptr;
    if (partition == nullptr) {
      return std::nullopt;
    }
    const auto slot = BipedSlotFromName(a_text);
    if (!slot) {
      return std::nullopt;
    }
    partition->bipedSlot = *slot;
    return SetSource{name, kind};
  };
}

[[nodiscard]] FieldBinding BindBakeBones(std::string a_name,
                                         SourceKind a_record) {
  return [name = std::move(a_name), record = std::move(a_record)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    SourceKind kind = record;
    BakeSource *bake = Get<BakeSource>(kind);
    BoneWeightBake *bones =
        bake != nullptr ? Get<BoneWeightBake>(bake->bake) : nullptr;
    if (bones == nullptr) {
      return std::nullopt;
    }
    bones->bones = SplitNames(a_text);
    return SetSource{name, kind};
  };
}

[[nodiscard]] FieldBinding BindClusterWeights(std::string a_name,
                                              SourceKind a_record) {
  return [name = std::move(a_name), record = std::move(a_record)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    SourceKind kind = record;
    MaterialClustersSource *clusters = Get<MaterialClustersSource>(kind);
    const auto weights = FiveNumbers(a_text);
    if (clusters == nullptr || !weights) {
      return std::nullopt;
    }
    clusters->settings.weights =
        ChannelWeights{(*weights)[0], (*weights)[1], (*weights)[2],
                       (*weights)[3], (*weights)[4]};
    return SetSource{name, kind};
  };
}

[[nodiscard]] FieldBinding BindLightBonesKind(std::size_t a_output) {
  return [a_output](const std::string &a_text) -> std::optional<RecipeEdit> {
    if (a_text == "skinned") {
      return SetLightBones{a_output, SkinnedBones{}};
    }
    if (a_text == "named") {
      return SetLightBones{a_output, NamedBones{{"NPC Spine2 [Spn2]"}}};
    }
    return std::nullopt;
  };
}

[[nodiscard]] FieldBinding BindLightSkinnedMax(std::size_t a_output,
                                               std::string a_minShare) {
  return [a_output, minShare = std::move(a_minShare)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto count = ParseCount(a_text);
    const auto share = ParseParam(minShare);
    const float *shareValue = share ? Get<float>(*share) : nullptr;
    if (!count || shareValue == nullptr) {
      return std::nullopt;
    }
    return SetLightBones{a_output, SkinnedBones{*count, *shareValue}};
  };
}

[[nodiscard]] FieldBinding BindLightSkinnedMinShare(std::size_t a_output,
                                                    std::string a_max) {
  return [a_output, max = std::move(a_max)](
             const std::string &a_text) -> std::optional<RecipeEdit> {
    const auto count = ParseCount(max);
    const auto share = ParseParam(a_text);
    const float *shareValue = share ? Get<float>(*share) : nullptr;
    if (!count || shareValue == nullptr || *shareValue < 0.0f ||
        *shareValue > 1.0f) {
      return std::nullopt;
    }
    return SetLightBones{a_output, SkinnedBones{*count, *shareValue}};
  };
}

[[nodiscard]] FieldBinding BindLightNames(std::size_t a_output) {
  return [a_output](const std::string &a_text) -> std::optional<RecipeEdit> {
    auto names = SplitNames(a_text);
    if (names.empty()) {
      return std::nullopt;
    }
    return SetLightBones{a_output, NamedBones{std::move(names)}};
  };
}
}

FormField CurveTextField(const std::string &a_curve,
                         const std::string &a_text) {
  return TextEntryField({.name = a_curve,
                         .kind = FieldKind::kCurve,
                         .text = a_text,
                         .bind = BindCurveText(a_curve)});
}

FormField MaskTextField(const std::string &a_mask, const std::string &a_text) {
  return TextEntryField({.name = a_mask,
                         .kind = FieldKind::kMask,
                         .text = a_text,
                         .bind = BindMaskText(a_mask)});
}

namespace {
[[nodiscard]] FormField
SourceFieldOf(const Inspector &a_inspector,
              const std::vector<std::string> &a_sourceNames) {
  FormField source = ValueField(
      {.name = "source",
       .kind = FieldKind::kLayerSource,
       .text = a_inspector.row.source,
       .names = a_sourceNames,
       .bind = BindLayerSource(a_inspector.output, a_inspector.layer),
       .value = std::nullopt,
       .detail =
           DetailWhen(a_inspector.source.has_value(), FieldDetail::kSource)});
  source.creators = Creators(kImageCreators);
  source.create = [bind = source.bind](const std::string &a_creator,
                                       const RecipeRow &a_recipe) {
    return CreateImage(a_creator, a_recipe, bind);
  };
  return source;
}

[[nodiscard]] FormField CurveFieldOf(const Inspector &a_inspector) {
  FormField curve = ValueField(
      {.name = "curve",
       .kind = FieldKind::kCurve,
       .text = a_inspector.row.curve,
       .names = a_inspector.curves,
       .bind = BindLayerCurve(a_inspector.output, a_inspector.layer),
       .value = std::nullopt,
       .detail = DetailWhen(a_inspector.curve.has_value(), FieldDetail::kCurve),
       .allowEmpty = true});
  curve.creators = {"new curve"};
  curve.create = [bind = curve.bind](const std::string &,
                                     const RecipeRow &a_recipe) {
    return std::optional<Created>{WireInto(Create(NewCurve{}, a_recipe), bind)};
  };
  return curve;
}

[[nodiscard]] FormField OpacityFieldOf(const Inspector &a_inspector) {
  FormField opacity = ValueField(
      {.name = "opacity",
       .kind = FieldKind::kScalar,
       .text = a_inspector.row.opacityText,
       .names = a_inspector.scalarSignals,
       .bind = BindLayerOpacity(a_inspector.output, a_inspector.layer),
       .value = std::nullopt,
       .detail =
           DetailWhen(NamesSignal(a_inspector, a_inspector.row.opacityText),
                      FieldDetail::kOpacity)});
  opacity.workingRange = kUnitRange;
  opacity.units = "fraction";
  opacity.creators = Creators(kValueCreators);
  opacity.create = [current = a_inspector.row.opacityText,
                    bind = opacity.bind](const std::string &a_creator,
                                         const RecipeRow &a_recipe) {
    return CreateValue({.creator = a_creator,
                        .field = "opacity",
                        .current = current,
                        .colour = false,
                        .recipe = a_recipe,
                        .bind = bind});
  };
  return opacity;
}

[[nodiscard]] FormField ColourFieldOf(const Inspector &a_inspector) {
  FormField colour = ValueField(
      {.name = "colour",
       .kind = FieldKind::kColor,
       .text = a_inspector.row.color,
       .names = a_inspector.colorSignals,
       .bind = BindLayerColor(a_inspector.output, a_inspector.layer),
       .value = std::nullopt,
       .detail = DetailWhen(NamesSignal(a_inspector, a_inspector.row.color),
                            FieldDetail::kColor),
       .allowEmpty = true});
  colour.creators = Creators(kValueCreators);
  colour.create = [current = a_inspector.row.color, bind = colour.bind](
                      const std::string &a_creator, const RecipeRow &a_recipe) {
    return CreateValue({.creator = a_creator,
                        .field = "colour",
                        .current = current,
                        .colour = true,
                        .recipe = a_recipe,
                        .bind = bind});
  };
  return colour;
}

[[nodiscard]] FormField MaskFieldOf(const Inspector &a_inspector) {
  FormField mask = ReferenceField(
      {.name = "mask",
       .text = a_inspector.row.mask,
       .names = a_inspector.masks,
       .allowEmpty = true,
       .bind = BindLayerMask(a_inspector.output, a_inspector.layer),
       .detail = DetailWhen(a_inspector.mask.has_value(), FieldDetail::kMask),
       .creators = {"new mask"}});
  mask.create = [bind = mask.bind](const std::string &a_creator,
                                   const RecipeRow &a_recipe) {
    return CreateImage(a_creator, a_recipe, bind);
  };
  return mask;
}
}

std::vector<FormField> InspectorForm(const Inspector &a_inspector) {
  const Inspector &in = a_inspector;
  const auto sourceNames = Joined(in.sources, in.masks);
  std::vector<FormField> form;
  form.push_back(SourceFieldOf(in, sourceNames));
  form.push_back(CurveFieldOf(in));
  form.push_back(BlendField("blend", in.row.blend, in.blends,
                            BindLayerBlend(in.output, in.layer)));
  form.push_back(OpacityFieldOf(in));
  form.push_back(ColourFieldOf(in));
  form.push_back(MaskFieldOf(in));
  FormField channels =
      ValueField({.name = "channels",
                  .kind = FieldKind::kChannels,
                  .text = in.row.channels,
                  .names = {},
                  .bind = BindLayerChannels(in.output, in.layer)});
  channels.channelMask = ChannelsOf(in.slot);
  form.push_back(std::move(channels));
  return form;
}

std::vector<FormField> ScalarForm(const LayerStack &a_stack) {
  std::vector<FormField> form;
  form.reserve(a_stack.scalars.size());
  for (const auto &scalar : a_stack.scalars) {
    const auto field = ParseScalarField(scalar.name);
    const bool colour = field == std::optional{ScalarField::kColor};
    const auto &names = colour ? a_stack.colorSignals : a_stack.scalarSignals;
    const bool signal =
        IsWholeReference(scalar.text) &&
        std::ranges::contains(names, ReferenceName(scalar.text));
    FormField row =
        ValueField({.name = scalar.name,
                    .kind = colour ? FieldKind::kColor : FieldKind::kScalar,
                    .text = scalar.text,
                    .names = names,
                    .bind = BindScalar(a_stack.output, field),
                    .detail = signal ? std::optional{FieldDetail::kSignal}
                                     : std::nullopt});
    if (field == ScalarField::kWeight || field == ScalarField::kRoughness ||
        field == ScalarField::kMicrofacetRoughness ||
        field == ScalarField::kDensityRandomization ||
        field == ScalarField::kLevel) {
      row.workingRange = kUnitRange;
      row.units = "fraction";
    }
    row.creators = Creators(kValueCreators);
    row.create = [name = scalar.name, current = scalar.text, colour,
                  bind = row.bind](const std::string &a_creator,
                                   const RecipeRow &a_recipe) {
      return CreateValue({.creator = a_creator,
                          .field = name,
                          .current = current,
                          .colour = colour,
                          .recipe = a_recipe,
                          .bind = bind});
    };
    form.push_back(std::move(row));
  }
  return form;
}

std::optional<RecipeEdit> SignalValueEdit(const std::string &a_signal,
                                          const std::string &a_text) {
  if (const auto param = ParseParam(a_text)) {
    if (const float *number = Get<float>(*param)) {
      return SetConstant{a_signal, *number};
    }
  }
  if (const auto colour = LiteralColor(a_text)) {
    return SetConstant{a_signal, *colour};
  }
  if (!a_text.empty() && Program::Parse(a_text)) {
    return SetExpression{a_signal, a_text};
  }
  return std::nullopt;
}

namespace {
struct SignalContext {
  const std::string &name;
  const SignalKind &record;
  const SignalNames &names;
  const std::vector<std::string> &all;
};

void ConstantFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                    const ConstantSignal &a_constant) {
  const std::string text = Match(
      a_constant.value, [](float a_number) { return ParamText(a_number); },
      [](const Vec2 &a_pair) {
        return std::format("{}, {}", ParamText(a_pair.x), ParamText(a_pair.y));
      },
      [](const Vec3 &a_colour) { return LiteralColorText(a_colour); });
  a_form.push_back(TextEntryField({.name = "value",
                                   .kind = FieldKind::kSignalValue,
                                   .text = text,
                                   .bind = BindSignalValue(a_ctx.name)}));
}

void ExprFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                const ExprSignal &a_expr) {
  a_form.push_back(TextEntryField({.name = "value",
                                   .kind = FieldKind::kSignalValue,
                                   .text = a_expr.text,
                                   .bind = BindSignalValue(a_ctx.name)}));
}

void PulseFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const WaveSignal &a_pulse) {
  a_form.push_back(
      ParamField({.name = "base",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.base),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &WaveSignal::base, ParseParam)}));
  a_form.push_back(ParamField(
      {.name = "amplitude",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_pulse.amplitude),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &WaveSignal::amplitude, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "period",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.period),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &WaveSignal::period, ParseParam),
                  .units = "seconds"}));
  a_form.push_back(
      ParamField({.name = "phase",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.phase),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &WaveSignal::phase, ParseParam),
                  .workingRange = kUnitRange,
                  .units = "cycles"}));
  a_form.push_back(
      ChoiceField("waveform", std::string{NameOf(kWaveforms, a_pulse.waveform)},
                  WordsOf(kWaveforms),
                  BindSignalMember(a_ctx.name, a_ctx.record,
                                   &WaveSignal::waveform, WordOf(kWaveforms))));
}

void RampFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                const RampSignal &a_ramp) {
  a_form.push_back(
      ParamField({.name = "from",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_ramp.from),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &RampSignal::from, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "to",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_ramp.to),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &RampSignal::to, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "seconds",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_ramp.seconds),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &RampSignal::seconds, ParseParam),
                  .units = "seconds"}));
}

void EfshFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                const EfshSignal &a_efsh) {
  a_form.push_back(
      ChoiceField("field", std::string{NameOf(kEfshFields, a_efsh.field)},
                  WordsOf(kEfshFields),
                  BindSignalMember(a_ctx.name, a_ctx.record, &EfshSignal::field,
                                   WordOf(kEfshFields))));
  a_form.push_back(
      TextEntryField({.name = "record",
                      .kind = FieldKind::kText,
                      .text = a_efsh.record.text,
                      .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                               &EfshSignal::record, FormOf)}));
}

void ActorValueFields(std::vector<FormField> &a_form,
                      const SignalContext &a_ctx,
                      const ActorValueSignal &a_actorValue) {
  FormField actorValue = TextEntryField(
      {.name = "actorValue",
       .kind = FieldKind::kText,
       .text = a_actorValue.actorValue,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &ActorValueSignal::actorValue, TextOf)});
  actorValue.catalog = GameObjectKind::kActorValue;
  a_form.push_back(std::move(actorValue));
  a_form.push_back(ChoiceField(
      "measure", std::string{NameOf(kMeasures, a_actorValue.measure)},
      WordsOf(kMeasures),
      BindSignalMember(a_ctx.name, a_ctx.record, &ActorValueSignal::measure,
                       WordOf(kMeasures))));
}

void ActorStateFields(std::vector<FormField> &a_form,
                      const SignalContext &a_ctx,
                      const ActorStateSignal &a_state) {
  a_form.push_back(ChoiceField(
      "state", std::string{NameOf(kActorStates, a_state.kind)},
      WordsOf(kActorStates),
      BindSignalMember(a_ctx.name, a_ctx.record, &ActorStateSignal::kind,
                       WordOf(kActorStates))));
}

void EnchantmentFields(std::vector<FormField> &a_form,
                       const SignalContext &a_ctx,
                       const EnchantmentSignal &a_enchantment) {
  a_form.push_back(ChoiceField(
      "field", std::string{NameOf(kEnchantmentFields, a_enchantment.field)},
      WordsOf(kEnchantmentFields),
      BindSignalMember(a_ctx.name, a_ctx.record, &EnchantmentSignal::field,
                       WordOf(kEnchantmentFields))));
}

constexpr std::array<std::string, 3> kAnchorWords{"none", "world", "node"};

std::string_view AnchorWord(const TriggerAnchor &a_anchor) {
  return Match(
      a_anchor, [](const std::monostate &) { return "none"; },
      [](const WorldAnchor &) { return "world"; },
      [](const NodeAnchor &) { return "node"; });
}

std::optional<TriggerAnchor> AnchorFromWord(const std::string &a_word) {
  if (a_word == "none") {
    return TriggerAnchor{};
  }
  if (a_word == "world") {
    return TriggerAnchor{WorldAnchor{}};
  }
  if (a_word == "node") {
    return TriggerAnchor{NodeAnchor{}};
  }
  return std::nullopt;
}

std::optional<TriggerAnchor> AnchorNodeFromText(const std::string &a_text) {
  return TriggerAnchor{NodeAnchor{a_text}};
}

void EventFilterFields(std::vector<FormField> &a_form,
                       const SignalContext &a_ctx,
                       const EventFilter &a_filter) {
  a_form.push_back(
      TextEntryField({.name = "filter node",
                      .kind = FieldKind::kText,
                      .text = a_filter.node,
                      .bind = BindEventFilter(a_ctx.name, a_ctx.record,
                                              &EventFilter::node, AlwaysString),
                      .allowEmpty = true}));
  a_form.push_back(
      TextEntryField({.name = "filter arg",
                      .kind = FieldKind::kText,
                      .text = a_filter.arg,
                      .bind = BindEventFilter(a_ctx.name, a_ctx.record,
                                              &EventFilter::arg, AlwaysString),
                      .allowEmpty = true}));
  a_form.push_back(TextEntryField(
      {.name = "filter min",
       .kind = FieldKind::kText,
       .text = BoundText(a_filter.value.min),
       .bind = BindEventFilterBound(a_ctx.name, a_ctx.record, &ValueRange::min),
       .allowEmpty = true}));
  a_form.push_back(TextEntryField(
      {.name = "filter max",
       .kind = FieldKind::kText,
       .text = BoundText(a_filter.value.max),
       .bind = BindEventFilterBound(a_ctx.name, a_ctx.record, &ValueRange::max),
       .allowEmpty = true}));
}

void TriggerOriginFields(std::vector<FormField> &a_form,
                         const SignalContext &a_ctx,
                         const TriggerSignal &a_trigger) {
  Match(
      a_trigger.origin,
      [&](const EventOrigin &a_event) {
        FormField event = TextEntryField(
            {.name = "event",
             .kind = FieldKind::kText,
             .text = a_event.event,
             .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                       &EventOrigin::event, TextOf)});
        event.catalog = GameObjectKind::kAnimEvent;
        event.names = {std::string{Studio::kHitReceivedEvent},
                       std::string{Studio::kHitDealtEvent}};
        a_form.push_back(std::move(event));
        EventFilterFields(a_form, a_ctx, a_event.filter);
      },
      [&](const PluginOrigin &a_plugin) {
        a_form.push_back(TextEntryField(
            {.name = "id",
             .kind = FieldKind::kText,
             .text = a_plugin.id,
             .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                       &PluginOrigin::id, TextOf)}));
      },
      [&](const WhenOrigin &a_when) {
        a_form.push_back(ReferenceField(
            {.name = "when",
             .text = RefText(a_when.when),
             .names = a_ctx.all,
             .allowEmpty = false,
             .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                       &WhenOrigin::when, RefOf)}));
        a_form.push_back(ReferenceField(
            {.name = "value",
             .text = RefText(a_when.value),
             .names = a_ctx.all,
             .allowEmpty = true,
             .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                       &WhenOrigin::value, OptionalRefOf)}));
      });
}

void TriggerFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                   const TriggerSignal &a_trigger) {
  const std::size_t origin = a_trigger.origin.index();
  a_form.push_back(
      ChoiceField("origin",
                  std::string{origin < std::size(kTriggerOriginWords)
                                  ? kTriggerOriginWords[origin]
                                  : "?"},
                  WordsOf(kTriggerOriginWords),
                  BindTriggerOrigin(a_ctx.name, a_ctx.record)));
  TriggerOriginFields(a_form, a_ctx, a_trigger);
  a_form.push_back(ParamField(
      {.name = "lifetime",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_trigger.lifetime),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &TriggerSignal::lifetime, ParseParam),
       .units = "seconds"}));
  FormField max =
      ParamField({.name = "max",
                  .kind = FieldKind::kScalar,
                  .text = std::to_string(a_trigger.max),
                  .names = {},
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &TriggerSignal::max, ParseCount)});
  max.range = std::pair{1.0f, 64.0f};
  max.workingRange = max.range;
  max.integral = true;
  a_form.push_back(std::move(max));
  a_form.push_back(ChoiceField(
      "payload", std::string{NameOf(kValueTypes, a_trigger.payload)},
      WordsOf(kValueTypes),
      BindSignalMember(a_ctx.name, a_ctx.record, &TriggerSignal::payload,
                       WordOf(kValueTypes))));
  a_form.push_back(
      ChoiceField("anchor", std::string{AnchorWord(a_trigger.anchor)},
                  {kAnchorWords.begin(), kAnchorWords.end()},
                  BindSignalMember(a_ctx.name, a_ctx.record,
                                   &TriggerSignal::anchor, AnchorFromWord)));
  if (const auto *node = Get<NodeAnchor>(a_trigger.anchor)) {
    a_form.push_back(TextEntryField(
        {.name = "anchorNode",
         .kind = FieldKind::kText,
         .text = node->node,
         .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                  &TriggerSignal::anchor, AnchorNodeFromText),
         .allowEmpty = true}));
  }
}

void PayloadFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                   const PayloadSignal &a_payload) {
  a_form.push_back(ReferenceField(
      {.name = "trigger",
       .text = RefText(a_payload.trigger),
       .names = a_ctx.names.triggers,
       .allowEmpty = false,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &PayloadSignal::trigger, RefOf)}));
}

void CounterFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                   const CounterSignal &a_counter) {
  a_form.push_back(ReferenceField(
      {.name = "trigger",
       .text = RefText(a_counter.trigger),
       .names = a_ctx.names.triggers,
       .allowEmpty = false,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &CounterSignal::trigger, RefOf)}));
  a_form.push_back(ReferenceField(
      {.name = "reset",
       .text = RefText(a_counter.reset),
       .names = a_ctx.names.triggers,
       .allowEmpty = true,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record, &CounterSignal::reset,
                                OptionalRefOf)}));
  FormField cap = ParamField(
      {.name = "cap",
       .kind = FieldKind::kScalar,
       .text = ParamTextOf(a_counter.cap),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record, &CounterSignal::cap,
                                OptionalParamOf)});
  cap.allowEmpty = true;
  a_form.push_back(std::move(cap));
}

void AccumulateFields(std::vector<FormField> &a_form,
                      const SignalContext &a_ctx,
                      const AccumulateSignal &a_accumulate) {
  a_form.push_back(ReferenceField(
      {.name = "trigger",
       .text = RefText(a_accumulate.trigger),
       .names = a_ctx.names.triggers,
       .allowEmpty = false,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &AccumulateSignal::trigger, RefOf)}));
  a_form.push_back(ParamField(
      {.name = "decay",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_accumulate.decay),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &AccumulateSignal::decay, ParseParam),
       .units = "units/second"}));
}

void NoiseFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const NoiseSignal &a_noise) {
  a_form.push_back(
      ParamField({.name = "frequency",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_noise.frequency),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &NoiseSignal::frequency, ParseParam),
                  .units = "1/second"}));
  a_form.push_back(ParamField(
      {.name = "amplitude",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_noise.amplitude),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &NoiseSignal::amplitude, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "seed",
                  .kind = FieldKind::kScalar,
                  .text = std::to_string(a_noise.seed),
                  .names = {},
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &NoiseSignal::seed, SeedOf),
                  .integral = true}));
}

void GradientFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                    const GradientSignal &a_gradient) {
  a_form.push_back(
      ParamField({.name = "t",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_gradient.t),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &GradientSignal::t, ParseParam),
                  .workingRange = kUnitRange}));
}

void DeltaFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const RateSignal &a_delta) {
  a_form.push_back(
      ReferenceField({.name = "of",
                      .text = RefText(a_delta.of),
                      .names = a_ctx.all,
                      .allowEmpty = false,
                      .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                               &RateSignal::of, RefOf)}));
}

void ToRootFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                  const ToRootSignal &a_toRoot) {
  a_form.push_back(
      ReferenceField({.name = "of",
                      .text = RefText(a_toRoot.of),
                      .names = a_ctx.all,
                      .allowEmpty = false,
                      .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                               &ToRootSignal::of, RefOf)}));
}

void SmoothFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                  const SmoothSignal &a_smooth) {
  a_form.push_back(
      ReferenceField({.name = "of",
                      .text = RefText(a_smooth.of),
                      .names = a_ctx.all,
                      .allowEmpty = false,
                      .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                               &SmoothSignal::of, RefOf)}));
  a_form.push_back(
      ParamField({.name = "seconds",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_smooth.seconds),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &SmoothSignal::seconds, ParseParam),
                  .units = "seconds"}));
}
}

std::vector<FormField> SignalForm(const SignalRow &a_signal,
                                  const SignalNames &a_names) {
  std::vector<FormField> form;
  const std::string &name = a_signal.name;
  const SignalKind &record = a_signal.definition;
  form.push_back(ChoiceField("kind", std::string{SignalKindName(a_signal.kind)},
                             WordsOf(kSignalKinds), BindSignalKind(name)));
  const auto all = Joined(a_names.scalar, a_names.color);
  const SignalContext ctx{name, record, a_names, all};
  Match(
      record,
      [&](const ConstantSignal &a_constant) {
        ConstantFields(form, ctx, a_constant);
      },
      [&](const ExprSignal &a_expr) { ExprFields(form, ctx, a_expr); },
      [&](const WaveSignal &a_pulse) { PulseFields(form, ctx, a_pulse); },
      [&](const RampSignal &a_ramp) { RampFields(form, ctx, a_ramp); },
      [&](const EfshSignal &a_efsh) { EfshFields(form, ctx, a_efsh); },
      [&](const ActorValueSignal &a_actorValue) {
        ActorValueFields(form, ctx, a_actorValue);
      },
      [&](const ActorStateSignal &a_state) {
        ActorStateFields(form, ctx, a_state);
      },
      [&](const EnchantmentSignal &a_enchantment) {
        EnchantmentFields(form, ctx, a_enchantment);
      },
      [&](const TriggerSignal &a_trigger) {
        TriggerFields(form, ctx, a_trigger);
      },
      [&](const PayloadSignal &a_payload) {
        PayloadFields(form, ctx, a_payload);
      },
      [&](const CounterSignal &a_counter) {
        CounterFields(form, ctx, a_counter);
      },
      [&](const AccumulateSignal &a_accumulate) {
        AccumulateFields(form, ctx, a_accumulate);
      },
      [&](const NoiseSignal &a_noise) { NoiseFields(form, ctx, a_noise); },
      [&](const GradientSignal &a_gradient) {
        GradientFields(form, ctx, a_gradient);
      },
      [&](const RateSignal &a_delta) { DeltaFields(form, ctx, a_delta); },
      [&](const ToRootSignal &a_toRoot) { ToRootFields(form, ctx, a_toRoot); },
      [&](const SmoothSignal &a_smooth) { SmoothFields(form, ctx, a_smooth); });
  return form;
}

namespace {
struct SourceContext {
  const std::string &name;
  const SourceKind &record;
  const SignalNames &names;
};

void ImageFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                 const ImageSourceRow &a_source) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const ImageSourceRow &source = a_source;
  a_form.push_back(
      TextEntryField({.name = "path",
                      .kind = FieldKind::kText,
                      .text = source.path,
                      .bind = BindSourceMember(name, record, &ImageSource::path,
                                               AlwaysString)}));
  a_form.push_back(
      ChoiceField("channel", source.channel, WordsOf(kImageChannels),
                  BindSourceMember(name, record, &ImageSource::channel,
                                   ParseImageChannel)));
  a_form.push_back(ChoiceField(
      "space", source.space, WordsOf(kImageSpaces),
      BindSourceMember(name, record, &ImageSource::space, ParseImageSpace)));
  FormField scroll =
      ParamField({.name = "scroll",
                  .kind = FieldKind::kVec2,
                  .text = source.scroll,
                  .names = a_ctx.names.vec2,
                  .bind = BindSourceMember(name, record, &ImageSource::scroll,
                                           OptionalVec2Of)});
  scroll.units = "texture repeats/second";
  scroll.allowEmpty = true;
  a_form.push_back(std::move(scroll));
  FormField tile =
      ParamField({.name = "tile",
                  .kind = FieldKind::kVec2,
                  .text = source.tile,
                  .names = a_ctx.names.vec2,
                  .bind = BindSourceMember(name, record, &ImageSource::tile,
                                           OptionalVec2Of)});
  tile.units = "texture repeats";
  tile.allowEmpty = true;
  a_form.push_back(std::move(tile));
  a_form.push_back(ToggleField("mirrorU", source.mirrorU == "on",
                               BindImageMirror(name, record, 0)));
  a_form.push_back(ToggleField("mirrorV", source.mirrorV == "on",
                               BindImageMirror(name, record, 1)));
  a_form.push_back(ToggleField(
      "transpose", source.transpose == "on",
      BindSourceMember(name, record, &ImageSource::transpose, AlwaysOnOff)));
  a_form.push_back(ParamField(
      {.name = "mip",
       .kind = FieldKind::kScalar,
       .text = source.mip,
       .names = {},
       .bind = BindSourceMember(name, record, &ImageSource::mip, NumberOf),
       .workingRange = kImageMipRange,
       .units = "mip level",
       .integral = true}));
}

void MaterialFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                    const MaterialSourceRow &a_source) {
  a_form.push_back(ChoiceField(
      "channel", a_source.material, WordsOf(kMaterialChannels),
      BindSourceMember(a_ctx.name, a_ctx.record, &MaterialSource::channel,
                       ParseMaterialChannel)));
}

void BakeFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                const BakeSourceRow &a_source) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  a_form.push_back(ChoiceField(
      "bake", a_source.bake, WordsOf(kBakeKindWords),
      BindSourceMember(name, record, &BakeSource::bake, DefaultBakeKind)));
  if (a_source.bake == "partition") {
    a_form.push_back(ChoiceField("partition", a_source.partition,
                                 BipedSlotNames(),
                                 BindBakePartition(name, record)));
  } else if (a_source.bake == "boneWeight") {
    a_form.push_back(TextEntryField({.name = "bones",
                                     .kind = FieldKind::kText,
                                     .text = a_source.bones,
                                     .bind = BindBakeBones(name, record)}));
  }
}

void DistanceFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                    const DistanceSourceRow &a_source) {
  a_form.push_back(TextEntryField(
      {.name = "from",
       .kind = FieldKind::kText,
       .text = a_source.from,
       .bind = BindSourceMember(a_ctx.name, a_ctx.record, &DistanceSource::from,
                                DistanceFromOf)}));
}

void RippleFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                  const RippleSourceRow &a_source) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const RippleSourceRow &source = a_source;
  FormField trigger = ReferenceField(
      {.name = "trigger",
       .text = source.trigger,
       .names = a_ctx.names.triggers,
       .allowEmpty = false,
       .bind = BindSourceMember(name, record, &RippleSource::trigger, RefOf),
       .creators = {"new trigger"}});
  trigger.create = [bind = trigger.bind](const std::string &,
                                         const RecipeRow &a_recipe) {
    return std::optional<Created>{WireInto(
        Create(NewSignal{"trigger", TriggerSignal{}}, a_recipe), bind)};
  };
  a_form.push_back(std::move(trigger));
  a_form.push_back(
      ParamField({.name = "speed",
                  .kind = FieldKind::kScalar,
                  .text = source.speed,
                  .names = a_ctx.names.scalar,
                  .bind = BindSourceMember(name, record, &RippleSource::speed,
                                           ParseParam)}));
  a_form.push_back(
      ParamField({.name = "width",
                  .kind = FieldKind::kScalar,
                  .text = source.width,
                  .names = a_ctx.names.scalar,
                  .bind = BindSourceMember(name, record, &RippleSource::width,
                                           ParseParam)}));
  a_form.push_back(
      ParamField({.name = "decay",
                  .kind = FieldKind::kScalar,
                  .text = source.decay,
                  .names = a_ctx.names.scalar,
                  .bind = BindSourceMember(name, record, &RippleSource::decay,
                                           ParseParam)}));
  a_form.push_back(ChoiceField(
      "shape", source.shape, WordsOf(kRippleShapes),
      BindSourceMember(name, record, &RippleSource::shape, ParseRippleShape)));
}

void ClustersFields(std::vector<FormField> &a_form, const SourceContext &a_ctx,
                    const MaterialClustersSourceRow &a_source) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const MaterialClustersSourceRow &source = a_source;
  FormField clusters = ParamField(
      {.name = "clusters",
       .kind = FieldKind::kScalar,
       .text = source.clusters,
       .names = {},
       .bind = BindSourceMember(name, record, &MaterialClustersSource::settings,
                                &ClusterSettings::clusters, ClusterCountOf)});
  clusters.range = std::pair{1.0f, static_cast<float>(kMaxMaterialClusters)};
  clusters.workingRange = clusters.range;
  clusters.integral = true;
  a_form.push_back(std::move(clusters));
  a_form.push_back(TextEntryField({.name = "weights",
                                   .kind = FieldKind::kText,
                                   .text = source.weights,
                                   .bind = BindClusterWeights(name, record)}));
  a_form.push_back(ParamField(
      {.name = "seed",
       .kind = FieldKind::kScalar,
       .text = source.seed,
       .names = {},
       .bind = BindSourceMember(name, record, &MaterialClustersSource::settings,
                                &ClusterSettings::seed, SeedOf),
       .integral = true}));
  FormField iterations = ParamField(
      {.name = "iterations",
       .kind = FieldKind::kScalar,
       .text = source.iterations,
       .names = {},
       .bind = BindSourceMember(name, record, &MaterialClustersSource::settings,
                                &ClusterSettings::iterations, IterationsOf)});
  iterations.range = std::pair{1.0f, static_cast<float>(kMaxClusterIterations)};
  iterations.workingRange = iterations.range;
  iterations.integral = true;
  a_form.push_back(std::move(iterations));
}
}

std::vector<FormField> SourceForm(const SourceRow &a_source,
                                  const SignalNames &a_names) {
  std::vector<FormField> form;
  const std::string &name = a_source.name;
  form.push_back(ChoiceField(
      "kind",
      std::string{NameOf(kSourceKindWords, SourceRowKindId(a_source.kind))},
      WordsOf(kSourceKindWords), BindSourceKindChoice(name)));
  const std::optional<SourceKind> record = SourceKindOf(a_source);
  if (!record) {
    return form;
  }
  const SourceContext ctx{name, *record, a_names};
  Match(
      a_source.kind,
      [&](const ImageSourceRow &a_image) { ImageFields(form, ctx, a_image); },
      [&](const MaterialSourceRow &a_material) {
        MaterialFields(form, ctx, a_material);
      },
      [&](const BakeSourceRow &a_bake) { BakeFields(form, ctx, a_bake); },
      [&](const DistanceSourceRow &a_distance) {
        DistanceFields(form, ctx, a_distance);
      },
      [&](const RippleSourceRow &a_ripple) {
        RippleFields(form, ctx, a_ripple);
      },
      [&](const MaterialClustersSourceRow &a_clusters) {
        ClustersFields(form, ctx, a_clusters);
      });
  return form;
}

std::vector<FormField> RecipeHeaderForm(const RecipeRow &a_recipe) {
  std::vector<FormField> form;
  form.push_back(TextEntryField({.name = "priority",
                                 .kind = FieldKind::kText,
                                 .text = std::to_string(a_recipe.priority),
                                 .bind = BindPriority(),
                                 .allowEmpty = true}));
  form.push_back(ChoiceField("merge",
                             std::string{MergeModeName(a_recipe.mergeMode)},
                             WordsOf(kMergeModes), BindMerge()));
  form.push_back(TextEntryField({.name = "clockSpeed",
                                 .kind = FieldKind::kText,
                                 .text = ParamText(a_recipe.clockSpeed),
                                 .bind = BindClockSpeed()}));
  return form;
}

OutputHeader OutputHeaderForm(std::size_t a_output, bool a_replace,
                              const Selector &a_selector) {
  OutputHeader header;
  header.fields.push_back(
      ToggleField("replace", a_replace, BindOutputReplace(a_output)));
  header.selector = SelectorViewOf(a_selector);
  return header;
}

namespace {
void LightColorFields(std::vector<FormField> &a_form, const LightRow &a_light,
                      const SignalNames &a_names) {
  const std::size_t output = a_light.output;
  a_form.push_back(
      ParamField({.name = "color",
                  .kind = FieldKind::kColor,
                  .text = a_light.color,
                  .names = a_names.color,
                  .bind = BindLightVector(output, LightVector::kColor)}));
  a_form.push_back(
      ParamField({.name = "intensity",
                  .kind = FieldKind::kScalar,
                  .text = a_light.intensity,
                  .names = a_names.scalar,
                  .bind = BindLightParam(output, LightParam::kIntensity),
                  .workingRange = kLightIntensityRange}));
  a_form.push_back(
      ParamField({.name = "size",
                  .kind = FieldKind::kScalar,
                  .text = a_light.size,
                  .names = a_names.scalar,
                  .bind = BindLightParam(output, LightParam::kSize),
                  .workingRange = kLightSizeRange}));
  a_form.push_back(
      ParamField({.name = "cutoff",
                  .kind = FieldKind::kScalar,
                  .text = a_light.cutoff,
                  .names = a_names.scalar,
                  .bind = BindLightParam(output, LightParam::kCutoff),
                  .workingRange = kLightCutoffRange}));
  a_form.push_back(
      ParamField({.name = "offset",
                  .kind = FieldKind::kVector,
                  .text = a_light.offset,
                  .names = a_names.color,
                  .bind = BindLightVector(output, LightVector::kOffset)}));
}

void LightShapeFields(std::vector<FormField> &a_form, const LightRow &a_light) {
  const std::size_t output = a_light.output;
  a_form.push_back(
      ToggleField("shadow", a_light.shadow, BindLightShadow(output)));
  a_form.push_back(
      ToggleField("replace", a_light.replace, BindLightReplace(output)));
  const bool skinned = a_light.bones != "named";
  a_form.push_back(ChoiceField("bones", skinned ? "skinned" : "named",
                               {"skinned", "named"},
                               BindLightBonesKind(output)));
  if (skinned) {
    FormField max = ValueField(
        {.name = "max",
         .kind = FieldKind::kScalar,
         .text = a_light.bonesMax,
         .names = {},
         .bind = BindLightSkinnedMax(output, a_light.bonesMinShare)});
    max.range = std::pair{1.0f, 64.0f};
    max.workingRange = max.range;
    max.integral = true;
    a_form.push_back(std::move(max));
    FormField minShare = ValueField(
        {.name = "minShare",
         .kind = FieldKind::kScalar,
         .text = a_light.bonesMinShare,
         .names = {},
         .bind = BindLightSkinnedMinShare(output, a_light.bonesMax)});
    minShare.range = kUnitRange;
    minShare.workingRange = minShare.range;
    minShare.units = "fraction";
    a_form.push_back(std::move(minShare));
  } else {
    a_form.push_back(TextEntryField({.name = "names",
                                     .kind = FieldKind::kText,
                                     .text = a_light.bonesNames,
                                     .bind = BindLightNames(output)}));
  }
}
}

std::vector<FormField> LightForm(const LightRow &a_light,
                                 const SignalNames &a_names) {
  std::vector<FormField> form;
  if (!a_light.present) {
    return form;
  }
  LightColorFields(form, a_light, a_names);
  LightShapeFields(form, a_light);
  return form;
}

namespace {
void ShellMaterialFields(std::vector<FormField> &a_form,
                         const ShellRow &a_shell, const SignalNames &a_names) {
  a_form.push_back(ChoiceField("material",
                               std::string{ShellMaterialName(a_shell.material)},
                               WordsOf(kShellMaterials), BindShellMaterial()));
  a_form.push_back(ChoiceField("blend",
                               std::string{ShellBlendName(a_shell.blend)},
                               WordsOf(kShellBlends), BindShellBlend()));
  a_form.push_back(
      ToggleField("depthBias", a_shell.depthBias, BindShellDepthBias()));
  FormField alphaTest = ValueField({.name = "alphaTest",
                                    .kind = FieldKind::kScalar,
                                    .text = ParamText(a_shell.alphaTest),
                                    .names = {},
                                    .bind = BindShellAlphaTest(),
                                    .units = "fraction"});
  alphaTest.range = kUnitRange;
  alphaTest.workingRange = alphaTest.range;
  a_form.push_back(std::move(alphaTest));
  a_form.push_back(ParamField({.name = "opacity",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.opacity,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kOpacity),
                               .workingRange = kUnitRange,
                               .units = "fraction"}));
  a_form.push_back(ParamField({.name = "rimPower",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.rimPower,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kRimPower),
                               .workingRange = kShellRimPowerRange}));
  a_form.push_back(ParamField({.name = "emissive",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.emissive,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kEmissive),
                               .workingRange = kShellEmissiveRange}));
}

void ShellPoseFields(std::vector<FormField> &a_form, const ShellRow &a_shell,
                     const SignalNames &a_names) {
  a_form.push_back(
      ParamField({.name = "inflate",
                  .kind = FieldKind::kVector,
                  .text = a_shell.inflate,
                  .names = a_names.color,
                  .bind = BindShellVector(ShellVector::kInflate)}));
  a_form.push_back(ParamField({.name = "offset",
                               .kind = FieldKind::kVector,
                               .text = a_shell.offset,
                               .names = a_names.color,
                               .bind = BindShellVector(ShellVector::kOffset)}));
  a_form.push_back(ParamField({.name = "scale",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.scale,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kScale),
                               .workingRange = kShellScaleRange}));
  a_form.push_back(
      ValueField({.name = "scalePoint",
                  .kind = FieldKind::kVector,
                  .text = LiteralColorText(a_shell.scalePoint),
                  .names = {},
                  .bind = BindShellPoint(ShellPoint::kScalePoint)}));
  a_form.push_back(ParamField({.name = "spin",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.spin,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kSpin)}));
  a_form.push_back(ValueField({.name = "spinAxis",
                               .kind = FieldKind::kVector,
                               .text = LiteralColorText(a_shell.spinAxis),
                               .names = {},
                               .bind = BindShellPoint(ShellPoint::kSpinAxis)}));
}
}

std::vector<FormField> ShellForm(const ShellRow &a_shell,
                                 const SignalNames &a_names) {
  std::vector<FormField> form;
  ShellMaterialFields(form, a_shell, a_names);
  ShellPoseFields(form, a_shell, a_names);
  return form;
}

}
