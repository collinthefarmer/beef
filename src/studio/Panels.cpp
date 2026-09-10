#include "studio/Panels.h"

#include "recipe/Expression.h"
#include "recipe/Words.h"
#include "studio/Fields.h"
#include "studio/Forms.h"
#include "studio/Names.h"
#include "studio/Selection.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool IsWholeReference(std::string_view a_text) noexcept {
  return a_text.size() > 1 && a_text.starts_with('@') &&
         a_text.find(',') == std::string_view::npos;
}

[[nodiscard]] bool IsMaterialOutput(const OutputRow &a_output) noexcept {
  return a_output.target != Target::kLight;
}

[[nodiscard]] bool WritesCell(const OutputRow &a_output, Surface a_surface,
                              Slot a_slot) noexcept {
  return IsMaterialOutput(a_output) && a_output.surface == a_surface &&
         a_output.slot == a_slot;
}

[[nodiscard]] bool OutputIsolated(const View &a_view,
                                  const std::string &a_recipe,
                                  std::size_t a_output) noexcept {
  return a_view.isolateRecipe == a_recipe && a_view.isolateOutput >= 0 &&
         static_cast<std::size_t>(a_view.isolateOutput) == a_output;
}

[[nodiscard]] bool LayerSoloed(const View &a_view, const std::string &a_recipe,
                               std::size_t a_output,
                               std::size_t a_layer) noexcept {
  return OutputIsolated(a_view, a_recipe, a_output) &&
         a_view.isolateLayer >= 0 &&
         static_cast<std::size_t>(a_view.isolateLayer) == a_layer;
}

[[nodiscard]] std::vector<Blend> BlendsFor(Slot a_slot) {
  std::vector<Blend> blends;
  for (const auto &row : kBlends) {
    if (BlendAllowed(a_slot, row.value)) {
      blends.push_back(row.value);
    }
  }
  return blends;
}

[[nodiscard]] const PictureRow *FindImage(const std::vector<PictureRow> &a_rows,
                                          std::string_view a_name) noexcept {
  const auto it = std::ranges::find(a_rows, a_name, &PictureRow::name);
  return it == a_rows.end() ? nullptr : &*it;
}

[[nodiscard]] const SignalRow *FindSignalRow(const RecipeRow &a_recipe,
                                             std::string_view a_name) noexcept {
  const auto it = std::ranges::find(a_recipe.signals, a_name, &SignalRow::name);
  return it == a_recipe.signals.end() ? nullptr : &*it;
}

[[nodiscard]] const TextRow *FindCurveRow(const RecipeRow &a_recipe,
                                          std::string_view a_name) noexcept {
  const auto it = std::ranges::find(a_recipe.curves, a_name, &TextRow::name);
  return it == a_recipe.curves.end() ? nullptr : &*it;
}

[[nodiscard]] const GeometryRow *
FindGeometry(const RecipeRow &a_recipe, std::string_view a_name) noexcept {
  const auto it =
      std::ranges::find(a_recipe.geometries, a_name, &GeometryRow::name);
  return it == a_recipe.geometries.end() ? nullptr : &*it;
}

[[nodiscard]] bool MergesBefore(const PieceRow &a_piece,
                                const RecipeRow &a_other,
                                const RecipeRow &a_recipe) noexcept {
  const auto mine =
      std::ranges::find(a_piece.recipes, a_recipe.id, &RecipeRow::id);
  const auto theirs =
      std::ranges::find(a_piece.recipes, a_other.id, &RecipeRow::id);
  if (mine == a_piece.recipes.end() || theirs == a_piece.recipes.end()) {
    return a_other.priority <= a_recipe.priority;
  }
  return theirs < mine;
}

void FillForeignRows(LayerStack &a_stack, const PieceRow &a_piece,
                     const RecipeRow &a_recipe, const GeometryRow &a_geometry) {
  const bool known = std::ranges::find(a_piece.recipes, a_recipe.id,
                                       &RecipeRow::id) != a_piece.recipes.end();
  std::optional<std::size_t> mine;
  for (const auto &output : a_geometry.outputs) {
    if (known && output.merged &&
        WritesCell(output, a_stack.surface, a_stack.slot)) {
      mine = output.merge;
      break;
    }
  }
  struct Neighbour {
    std::size_t merge = 0;
    const RecipeRow *recipe = nullptr;
    const OutputRow *output = nullptr;
  };
  std::vector<Neighbour> neighbours;
  for (const auto &other : a_piece.recipes) {
    if (other.id == a_recipe.id) {
      continue;
    }
    const auto *geometry = FindGeometry(other, a_geometry.name);
    if (geometry == nullptr) {
      continue;
    }
    for (const auto &output : geometry->outputs) {
      if (!output.merged ||
          !WritesCell(output, a_stack.surface, a_stack.slot)) {
        continue;
      }
      neighbours.push_back(Neighbour{output.merge, &other, &output});
    }
  }
  std::ranges::stable_sort(neighbours, {}, &Neighbour::merge);
  for (const auto &neighbour : neighbours) {
    const bool below = mine
                           ? neighbour.merge < *mine
                           : MergesBefore(a_piece, *neighbour.recipe, a_recipe);
    auto &rows = below ? a_stack.below : a_stack.above;
    for (const auto &layer : neighbour.output->layers) {
      rows.push_back(
          ForeignRow{neighbour.recipe->id, neighbour.recipe->priority, layer});
    }
  }
}

void AddSignalNamed(std::vector<SignalRow> &a_signals,
                    const RecipeRow &a_recipe, std::string_view a_text) {
  if (!IsWholeReference(a_text)) {
    return;
  }
  const auto *signal = FindSignalRow(a_recipe, ReferenceName(a_text));
  if (signal != nullptr &&
      std::ranges::find(a_signals, signal->name, &SignalRow::name) ==
          a_signals.end()) {
    a_signals.push_back(*signal);
  }
}

[[nodiscard]] bool NamesSignal(const Inspector &a_inspector,
                               const std::string &a_text) {
  return a_text.starts_with('@') &&
         std::ranges::find(a_inspector.signals, ReferenceName(a_text),
                           &SignalRow::name) != a_inspector.signals.end();
}

[[nodiscard]] std::optional<FieldDetail> DetailWhen(bool a_present,
                                                    FieldDetail a_detail) {
  return a_present ? std::optional{a_detail} : std::nullopt;
}

[[nodiscard]] std::string OnOff(bool a_on) { return a_on ? "on" : "off"; }

[[nodiscard]] std::string PointText(const Vec3 &a_point) {
  return LiteralColorText(a_point);
}

[[nodiscard]] std::vector<std::string>
Joined(std::span<const std::string> a_a, std::span<const std::string> a_b) {
  std::vector<std::string> out(a_a.begin(), a_a.end());
  out.insert(out.end(), a_b.begin(), a_b.end());
  return out;
}

[[nodiscard]] std::optional<std::uint32_t> WholeNumber(std::string_view a_text,
                                                       std::uint32_t a_max) {
  while (!a_text.empty() && a_text.front() == ' ') {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() && a_text.back() == ' ') {
    a_text.remove_suffix(1);
  }
  std::uint32_t value = 0;
  const auto result =
      std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
  if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() ||
      value > a_max) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::array<float, 5>>
FiveNumbers(std::string_view a_text) {
  std::array<float, 5> out{};
  std::size_t count = 0;
  std::size_t at = 0;
  while (at <= a_text.size()) {
    const auto comma = a_text.find(',', at);
    const auto part = a_text.substr(at, comma == std::string_view::npos
                                            ? std::string_view::npos
                                            : comma - at);
    const auto param = ParseParam(part);
    const float *number = param ? Get<float>(*param) : nullptr;
    if (number == nullptr || count >= out.size()) {
      return std::nullopt;
    }
    out[count++] = *number;
    if (comma == std::string_view::npos) {
      break;
    }
    at = comma + 1;
  }
  return count == out.size() ? std::optional{out} : std::nullopt;
}

[[nodiscard]] std::vector<std::string> SplitNames(std::string_view a_text) {
  std::vector<std::string> names;
  std::size_t at = 0;
  while (at <= a_text.size()) {
    const auto comma = a_text.find(',', at);
    auto part = a_text.substr(at, comma == std::string_view::npos
                                      ? std::string_view::npos
                                      : comma - at);
    while (!part.empty() && part.front() == ' ') {
      part.remove_prefix(1);
    }
    while (!part.empty() && part.back() == ' ') {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      names.emplace_back(part);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    at = comma + 1;
  }
  return names;
}

[[nodiscard]] std::optional<std::uint32_t> ParseCount(std::string_view a_text) {
  const auto param = ParseParam(a_text);
  const float *number = param ? Get<float>(*param) : nullptr;
  if (number == nullptr || *number < 1.0f || *number > 64.0f) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*number);
}

[[nodiscard]] std::vector<std::string> BipedSlotNames() {
  std::vector<std::string> names;
  for (std::uint32_t slot = 30; slot <= 61; ++slot) {
    if (const auto name = BipedSlotName(slot)) {
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
  return ref ? std::optional{std::optional{*ref}} : std::nullopt;
}

[[nodiscard]] std::optional<std::optional<Param>>
OptionalParamOf(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<Param>{};
  }
  const auto param = ParseParam(a_text);
  return param ? std::optional{std::optional{*param}} : std::nullopt;
}

[[nodiscard]] std::optional<std::optional<Vec2Param>>
OptionalVec2Of(const std::string &a_text) {
  if (a_text.empty()) {
    return std::optional<Vec2Param>{};
  }
  const auto pair = ParseVec2Param(a_text);
  return pair ? std::optional{std::optional{*pair}} : std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> CountOf(const std::string &a_text) {
  const auto param = ParseParam(a_text);
  const float *number = param ? Get<float>(*param) : nullptr;
  return number != nullptr && *number >= 0.0f
             ? std::optional{static_cast<std::uint32_t>(*number)}
             : std::nullopt;
}

[[nodiscard]] std::optional<std::string> TextOf(const std::string &a_text) {
  return a_text.empty() ? std::nullopt : std::optional{a_text};
}

[[nodiscard]] std::optional<std::string> StringAny(const std::string &a_text) {
  return std::optional{a_text};
}

[[nodiscard]] std::optional<bool> OnOffAny(const std::string &a_text) {
  return std::optional{a_text == "on"};
}

[[nodiscard]] std::optional<float> NumberOf(const std::string &a_text) {
  const auto param = ParseParam(a_text);
  const float *number = param ? Get<float>(*param) : nullptr;
  return number != nullptr ? std::optional{*number} : std::nullopt;
}

[[nodiscard]] std::optional<FormRef> FormOf(const std::string &a_text) {
  return a_text.empty() ? std::nullopt : std::optional{FormRef::From(a_text)};
}

[[nodiscard]] std::optional<std::variant<std::string, Vec3>>
DistanceFromOf(const std::string &a_text) {
  if (const auto point = LiteralColor(a_text)) {
    return std::variant<std::string, Vec3>{*point};
  }
  return std::variant<std::string, Vec3>{a_text};
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

[[nodiscard]] std::vector<RecipeEdit>
CreateImage(const std::string &a_creator, std::span<const std::string> a_taken,
            const FieldBinding &a_bind) {
  std::vector<RecipeEdit> edits;
  const auto make = [&](const std::string &a_name, RecipeEdit a_add) {
    edits.push_back(std::move(a_add));
    if (const auto bound = a_bind(ReferenceText(a_name))) {
      edits.push_back(*bound);
    }
  };
  if (a_creator == "new mask") {
    const auto name = UniqueName("mask", a_taken);
    make(name, AddMask{name});
    return edits;
  }
  const std::string_view word = a_creator.starts_with("new ")
                                    ? std::string_view{a_creator}.substr(4)
                                    : std::string_view{a_creator};
  if (const auto kind = DefaultSourceKind(word)) {
    const auto name = UniqueName(word, a_taken);
    make(name, AddSource{name, *kind});
  }
  return edits;
}

struct CreateValueSpec {
  const std::string &creator;
  std::string_view field;
  const std::string &current;
  bool colour;
  std::span<const std::string> taken;
  const FieldBinding &bind;
};

[[nodiscard]] std::vector<RecipeEdit>
CreateValue(const CreateValueSpec &a_spec) {
  std::vector<RecipeEdit> edits;
  const auto bindTo = [&](const std::string &a_name) {
    if (const auto bound = a_spec.bind(ReferenceText(a_name))) {
      edits.push_back(*bound);
    }
  };
  if (a_spec.creator == "promote to signal") {
    const auto name = UniqueName(a_spec.field, a_spec.taken);
    Value value = 0.0f;
    if (a_spec.colour) {
      const auto colour = LiteralColor(a_spec.current);
      if (!colour) {
        return edits;
      }
      value = *colour;
    } else {
      const auto param = ParseParam(a_spec.current);
      const float *number = param ? Get<float>(*param) : nullptr;
      if (number == nullptr) {
        return edits;
      }
      value = *number;
    }
    edits.emplace_back(AddSignal{name});
    edits.emplace_back(SetConstant{name, value});
    bindTo(name);
    return edits;
  }
  if (a_spec.creator == "new constant") {
    const auto name = UniqueName("signal", a_spec.taken);
    edits.emplace_back(AddSignal{name});
    if (a_spec.colour) {
      edits.emplace_back(SetConstant{name, Vec3{1.0f, 1.0f, 1.0f}});
    }
    bindTo(name);
    return edits;
  }
  if (a_spec.creator == "new expression") {
    const auto name = UniqueName("signal", a_spec.taken);
    edits.emplace_back(AddSignal{name});
    edits.emplace_back(SetExpression{name, a_spec.colour ? "[1, 1, 1]" : "1"});
    bindTo(name);
    return edits;
  }
  return edits;
}

struct ParamFieldSpec {
  std::string name;
  FieldKind kind;
  std::string text;
  std::vector<std::string> names;
  FieldBinding bind;
};

[[nodiscard]] FormField ParamField(ParamFieldSpec a_spec) {
  const bool valued =
      a_spec.kind == FieldKind::kScalar || a_spec.kind == FieldKind::kColor ||
      a_spec.kind == FieldKind::kVector || a_spec.kind == FieldKind::kVec2;
  const bool signal =
      valued && IsWholeReference(a_spec.text) &&
      std::ranges::find(a_spec.names, ReferenceName(a_spec.text)) !=
          a_spec.names.end();
  const bool offers =
      !a_spec.names.empty() &&
      (a_spec.kind == FieldKind::kScalar || a_spec.kind == FieldKind::kColor);
  FormField field = ValueField(
      {.name = a_spec.name,
       .kind = a_spec.kind,
       .text = a_spec.text,
       .names = a_spec.names,
       .bind = std::move(a_spec.bind),
       .detail = signal ? std::optional{FieldDetail::kSignal} : std::nullopt});
  if (offers) {
    field.creators = Creators(kValueCreators);
    field.create = [name = a_spec.name, current = a_spec.text,
                    colour = a_spec.kind == FieldKind::kColor,
                    taken = std::move(a_spec.names),
                    bind = field.bind](const std::string &a_creator) {
      return CreateValue({.creator = a_creator,
                          .field = name,
                          .current = current,
                          .colour = colour,
                          .taken = taken,
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
    partition->slot = *slot;
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
    clusters->roughness = (*weights)[0];
    clusters->metallic = (*weights)[1];
    clusters->occlusion = (*weights)[2];
    clusters->reflectance = (*weights)[3];
    clusters->luma = (*weights)[4];
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

std::optional<LayerStack> BuildStackView(const StackViewInput &a_input) {
  const PieceRow &a_piece = a_input.piece;
  const RecipeRow &a_recipe = a_input.recipe;
  const GeometryRow &a_geometry = a_input.geometry;
  const Selection &a_selection = a_input.selection;
  const View &a_view = a_input.view;
  const OutputRow *output = SelectedOutput(&a_geometry, a_selection);
  if (output == nullptr || !IsMaterialOutput(*output)) {
    return std::nullopt;
  }
  LayerStack stack;
  stack.output = output->index;
  stack.surface = output->surface;
  stack.slot = output->slot;
  stack.rows.reserve(output->layers.size());
  for (std::size_t i = 0; i < output->layers.size(); ++i) {
    LayerStackRow row;
    row.index = i;
    row.layer = output->layers[i];
    row.muted = a_view.LayerMuted(a_recipe.id, output->index, i);
    row.soloed = LayerSoloed(a_view, a_recipe.id, output->index, i);
    row.selected = a_selection.layer && *a_selection.layer == i;
    stack.rows.push_back(std::move(row));
  }
  FillForeignRows(stack, a_piece, a_recipe, a_geometry);
  stack.composite = output->texture;
  stack.animated = output->animated;
  stack.size = output->size;
  stack.problem = output->problem;
  stack.scalars = output->scalars;
  stack.blends = BlendsFor(output->slot);
  stack.masks = a_recipe.masks;
  for (const auto &signal : a_recipe.signals) {
    if (signal.type == ValueType::kScalar) {
      stack.scalarSignals.push_back(signal.name);
    } else if (signal.type == ValueType::kVec3) {
      stack.colorSignals.push_back(signal.name);
    }
  }
  stack.isolated = OutputIsolated(a_view, a_recipe.id, output->index);
  return stack;
}

std::optional<Inspector> BuildInspector(const RecipeRow &a_recipe,
                                        const GeometryRow &a_geometry,
                                        const Selection &a_selection) {
  const OutputRow *output = SelectedOutput(&a_geometry, a_selection);
  if (output == nullptr || !IsMaterialOutput(*output) || !a_selection.layer ||
      *a_selection.layer >= output->layers.size()) {
    return std::nullopt;
  }
  Inspector inspector;
  inspector.output = output->index;
  inspector.layer = *a_selection.layer;
  inspector.slot = output->slot;
  inspector.row = output->layers[inspector.layer];
  const LayerRow &row = inspector.row;

  if (IsWholeReference(row.source)) {
    const auto name = ReferenceName(row.source);
    const PictureRow *image = FindImage(a_geometry.sources, name);
    if (image == nullptr) {
      image = FindImage(a_geometry.masks, name);
    }
    if (image != nullptr) {
      inspector.source = *image;
    }
  }
  if (!row.mask.empty()) {
    if (const PictureRow *image =
            FindImage(a_geometry.masks, ReferenceName(row.mask))) {
      inspector.mask = *image;
    }
  }
  AddSignalNamed(inspector.signals, a_recipe, row.opacityText);
  AddSignalNamed(inspector.signals, a_recipe, row.color);
  if (IsWholeReference(row.curve)) {
    if (const TextRow *curve =
            FindCurveRow(a_recipe, ReferenceName(row.curve))) {
      inspector.curve = *curve;
    }
  }
  inspector.blends = BlendsFor(output->slot);
  for (const auto &source : a_geometry.sources) {
    inspector.sources.push_back(source.name);
  }
  for (const auto &mask : a_geometry.masks) {
    inspector.masks.push_back(mask.name);
  }
  for (const auto &curve : a_recipe.curves) {
    inspector.curves.push_back(curve.name);
  }
  for (const auto &signal : a_recipe.signals) {
    if (signal.type == ValueType::kScalar) {
      inspector.scalarSignals.push_back(signal.name);
    } else if (signal.type == ValueType::kVec3) {
      inspector.colorSignals.push_back(signal.name);
    }
  }
  return inspector;
}

SignalNames SignalNamesOf(const RecipeRow &a_recipe) {
  SignalNames names;
  for (const auto &signal : a_recipe.signals) {
    if (signal.type == ValueType::kScalar) {
      names.scalar.push_back(signal.name);
    } else if (signal.type == ValueType::kVec3) {
      names.color.push_back(signal.name);
    } else if (signal.type == ValueType::kVec2) {
      names.vec2.push_back(signal.name);
    }
    if (signal.kind == SignalKindId::kTrigger) {
      names.triggers.push_back(signal.name);
    }
  }
  return names;
}

SignalList BuildSignalList(const RecipeRow &a_recipe, const Layout &a_layout) {
  SignalList list;
  for (const auto &signal : a_recipe.signals) {
    if (SignalKindTunable(signal.kind)) {
      list.tunable.push_back(signal);
    } else if (a_layout.developerSignals) {
      list.developer.push_back(signal);
    }
  }
  return list;
}

LightRow LightRowOf(const Recipe &a_recipe) {
  LightRow row;
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    const LightOutput *light = Get<LightOutput>(a_recipe.outputs[i]);
    if (light == nullptr) {
      continue;
    }
    row.present = true;
    row.output = i;
    row.color = Vec3ParamText(light->color);
    row.intensity = ParamText(light->intensity);
    row.size = ParamText(light->size);
    row.cutoff = ParamText(light->cutoff);
    row.offset = Vec3ParamText(light->offset);
    row.shadow = light->shadow;
    row.replace = light->replace;
    Match(
        light->bones,
        [&](const SkinnedBones &a_bones) {
          row.bones = "skinned";
          row.bonesMax = std::to_string(a_bones.max);
          row.bonesMinShare = ParamText(a_bones.minShare);
        },
        [&](const NamedBones &a_bones) {
          row.bones = "named";
          for (const auto &bone : a_bones.bones) {
            row.bonesNames += (row.bonesNames.empty() ? "" : ", ") + bone;
          }
        });
    break;
  }
  return row;
}

ShellRow ShellRowOf(const Recipe &a_recipe) {
  const ShellSettings &shell = a_recipe.shell;
  ShellRow row;
  row.material = shell.material;
  row.blend = shell.blend;
  row.depthBias = shell.depthBias;
  row.alphaTest = shell.alphaTest;
  row.alpha = ParamText(shell.alpha);
  row.rimPower = ParamText(shell.rimPower);
  row.emissive = ParamText(shell.emissive);
  row.inflate = Vec3ParamText(shell.pose.inflate);
  row.offset = Vec3ParamText(shell.pose.offset);
  row.scale = ParamText(shell.pose.scale);
  row.spin = ParamText(shell.pose.spin);
  row.scalePoint = shell.pose.scalePoint;
  row.spinAxis = shell.pose.spinAxis;
  return row;
}

SourceRow SourceRowOf(const Source &a_source, std::size_t a_references) {
  SourceRow row;
  row.name = a_source.name;
  row.kind = std::string{SourceKindName(a_source.kind)};
  row.references = a_references;
  Match(
      a_source.kind,
      [&](const ImageSource &a_image) {
        row.path = a_image.path;
        row.channel = std::string{ImageChannelName(a_image.channel)};
        row.space = std::string{ImageSpaceName(a_image.space)};
        row.scroll = a_image.scroll ? Vec2ParamText(*a_image.scroll) : "";
        row.tile = a_image.tile ? Vec2ParamText(*a_image.tile) : "";
        row.mirrorU = OnOff(a_image.mirror[0]);
        row.mirrorV = OnOff(a_image.mirror[1]);
        row.transpose = OnOff(a_image.transpose);
        row.mip = ParamText(a_image.mip);
      },
      [&](const MaterialSource &a_material) {
        row.material = std::string{MaterialChannelName(a_material.channel)};
      },
      [&](const BakeSource &a_bake) {
        row.bake = std::string{BakeKindName(a_bake.bake)};
        if (const PartitionBake *partition = Get<PartitionBake>(a_bake.bake)) {
          const auto name = BipedSlotName(partition->slot);
          row.partition =
              name ? std::string{*name} : std::to_string(partition->slot);
        }
        if (const BoneWeightBake *bones = Get<BoneWeightBake>(a_bake.bake)) {
          for (const auto &bone : bones->bones) {
            row.bones += (row.bones.empty() ? "" : ", ") + bone;
          }
        }
      },
      [&](const UvSource &a_uv) {
        row.axis = std::string{UvAxisName(a_uv.axis)};
      },
      [&](const DistanceSource &a_distance) {
        row.from = Match(
            a_distance.from, [](const std::string &a_node) { return a_node; },
            [](const Vec3 &a_point) { return PointText(a_point); });
      },
      [&](const RippleSource &a_ripple) {
        row.trigger = "@" + a_ripple.trigger.name;
        row.speed = ParamText(a_ripple.speed);
        row.width = ParamText(a_ripple.width);
        row.decay = ParamText(a_ripple.decay);
        row.shape = std::string{RippleShapeName(a_ripple.shape)};
      },
      [&](const MaterialClustersSource &a_clusters) {
        row.clusters = std::to_string(a_clusters.clusters);
        row.weights = std::format(
            "{}, {}, {}, {}, {}", ParamText(a_clusters.roughness),
            ParamText(a_clusters.metallic), ParamText(a_clusters.occlusion),
            ParamText(a_clusters.reflectance), ParamText(a_clusters.luma));
        row.seed = std::to_string(a_clusters.seed);
        row.iterations = std::to_string(a_clusters.iterations);
      });
  return row;
}

namespace {
[[nodiscard]] std::optional<SourceKind> ImageKindOf(const SourceRow &a_row) {
  ImageSource image;
  image.path = a_row.path;
  const auto channel = ParseImageChannel(a_row.channel);
  const auto space = ParseImageSpace(a_row.space);
  const auto mip = ParseParam(a_row.mip);
  const float *mipValue = mip ? Get<float>(*mip) : nullptr;
  if (!channel || !space || mipValue == nullptr) {
    return std::nullopt;
  }
  image.channel = *channel;
  image.space = *space;
  image.mip = *mipValue;
  if (!a_row.scroll.empty()) {
    const auto scroll = ParseVec2Param(a_row.scroll);
    if (!scroll) {
      return std::nullopt;
    }
    image.scroll = *scroll;
  }
  if (!a_row.tile.empty()) {
    const auto tile = ParseVec2Param(a_row.tile);
    if (!tile) {
      return std::nullopt;
    }
    image.tile = *tile;
  }
  image.mirror = {a_row.mirrorU == "on", a_row.mirrorV == "on"};
  image.transpose = a_row.transpose == "on";
  return SourceKind{image};
}

[[nodiscard]] std::optional<SourceKind> MaterialKindOf(const SourceRow &a_row) {
  const auto channel = ParseMaterialChannel(a_row.material);
  return channel ? std::optional<SourceKind>{MaterialSource{*channel}}
                 : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> BakeKindOf(const SourceRow &a_row) {
  auto bake = DefaultBakeKind(a_row.bake);
  if (!bake) {
    return std::nullopt;
  }
  if (PartitionBake *partition = Get<PartitionBake>(*bake)) {
    const auto slot = BipedSlotFromName(a_row.partition);
    if (!slot) {
      return std::nullopt;
    }
    partition->slot = *slot;
  }
  if (BoneWeightBake *bones = Get<BoneWeightBake>(*bake)) {
    bones->bones = SplitNames(a_row.bones);
  }
  return SourceKind{BakeSource{*bake}};
}

[[nodiscard]] std::optional<SourceKind> UvKindOf(const SourceRow &a_row) {
  const auto axis = ParseUvAxis(a_row.axis);
  return axis ? std::optional<SourceKind>{UvSource{*axis}} : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> DistanceKindOf(const SourceRow &a_row) {
  DistanceSource distance;
  if (const auto point = LiteralColor(a_row.from)) {
    distance.from = *point;
  } else {
    distance.from = a_row.from;
  }
  return SourceKind{distance};
}

[[nodiscard]] std::optional<SourceKind> RippleKindOf(const SourceRow &a_row) {
  RippleSource ripple;
  if (!a_row.trigger.starts_with('@') || a_row.trigger.size() < 2) {
    return std::nullopt;
  }
  ripple.trigger = Ref{a_row.trigger.substr(1)};
  const auto speed = ParseParam(a_row.speed);
  const auto width = ParseParam(a_row.width);
  const auto decay = ParseParam(a_row.decay);
  const auto shape = ParseRippleShape(a_row.shape);
  if (!speed || !width || !decay || !shape) {
    return std::nullopt;
  }
  ripple.speed = *speed;
  ripple.width = *width;
  ripple.decay = *decay;
  ripple.shape = *shape;
  return SourceKind{ripple};
}

[[nodiscard]] std::optional<SourceKind> ClustersKindOf(const SourceRow &a_row) {
  MaterialClustersSource clusters;
  const auto count = WholeNumber(a_row.clusters, kMaxMaterialClusters);
  const auto weights = FiveNumbers(a_row.weights);
  const auto seed =
      WholeNumber(a_row.seed, std::numeric_limits<std::uint32_t>::max());
  const auto iterations = WholeNumber(a_row.iterations, kMaxClusterIterations);
  if (!count || *count < 1 || !weights || !seed || !iterations ||
      *iterations < 1) {
    return std::nullopt;
  }
  clusters.clusters = static_cast<std::uint8_t>(*count);
  clusters.roughness = (*weights)[0];
  clusters.metallic = (*weights)[1];
  clusters.occlusion = (*weights)[2];
  clusters.reflectance = (*weights)[3];
  clusters.luma = (*weights)[4];
  clusters.seed = *seed;
  clusters.iterations = *iterations;
  return SourceKind{clusters};
}
}

std::optional<SourceKind> SourceKindOf(const SourceRow &a_row) {
  if (a_row.kind == "image") {
    return ImageKindOf(a_row);
  }
  if (a_row.kind == "material") {
    return MaterialKindOf(a_row);
  }
  if (a_row.kind == "bake") {
    return BakeKindOf(a_row);
  }
  if (a_row.kind == "uv") {
    return UvKindOf(a_row);
  }
  if (a_row.kind == "distance") {
    return DistanceKindOf(a_row);
  }
  if (a_row.kind == "ripple") {
    return RippleKindOf(a_row);
  }
  if (a_row.kind == "materialClusters") {
    return ClustersKindOf(a_row);
  }
  return std::nullopt;
}

std::string_view FieldDetailName(FieldDetail a_detail) noexcept {
  switch (a_detail) {
  case FieldDetail::kSource:
    return "source";
  case FieldDetail::kCurve:
    return "curve";
  case FieldDetail::kOpacity:
    return "opacity";
  case FieldDetail::kColor:
    return "colour";
  case FieldDetail::kMask:
    return "mask";
  case FieldDetail::kSignal:
    return "signal";
  }
  return "?";
}

FormField RowNameField(RowKind a_kind, const std::string &a_name,
                       std::vector<std::string> a_taken) {
  FieldBinding bind =
      [a_kind,
       from = a_name](const std::string &a_text) -> std::optional<RecipeEdit> {
    if (!IsName(a_text)) {
      return std::nullopt;
    }
    switch (a_kind) {
    case RowKind::kSignal:
      return RenameSignal{from, a_text};
    case RowKind::kCurve:
      return RenameCurve{from, a_text};
    case RowKind::kSource:
      return RenameSource{from, a_text};
    case RowKind::kMask:
      return RenameMask{from, a_text};
    }
    return std::nullopt;
  };
  return ValueField({.name = a_name,
                     .kind = FieldKind::kName,
                     .text = a_name,
                     .names = std::move(a_taken),
                     .bind = std::move(bind)});
}

FormField CurveTextField(const std::string &a_curve,
                         const std::string &a_text) {
  return TextedField({.name = a_curve,
                      .kind = FieldKind::kCurve,
                      .text = a_text,
                      .bind = BindCurveText(a_curve)});
}

FormField MaskTextField(const std::string &a_mask, const std::string &a_text) {
  return TextedField({.name = a_mask,
                      .kind = FieldKind::kMask,
                      .text = a_text,
                      .bind = BindMaskText(a_mask)});
}

namespace {
[[nodiscard]] FormField
SourceFieldOf(const Inspector &a_in,
              const std::vector<std::string> &a_sourceNames) {
  FormField source = ValueField(
      {.name = "source",
       .kind = FieldKind::kLayerSource,
       .text = a_in.row.source,
       .names = a_sourceNames,
       .bind = BindLayerSource(a_in.output, a_in.layer),
       .value = std::nullopt,
       .detail = DetailWhen(a_in.source.has_value(), FieldDetail::kSource)});
  source.creators = Creators(kImageCreators);
  source.create = [taken = a_sourceNames,
                   bind = source.bind](const std::string &a_creator) {
    return CreateImage(a_creator, taken, bind);
  };
  return source;
}

[[nodiscard]] FormField CurveFieldOf(const Inspector &a_in) {
  FormField curve = ValueField(
      {.name = "curve",
       .kind = FieldKind::kCurve,
       .text = a_in.row.curve,
       .names = a_in.curves,
       .bind = BindLayerCurve(a_in.output, a_in.layer),
       .value = std::nullopt,
       .detail = DetailWhen(a_in.curve.has_value(), FieldDetail::kCurve),
       .allowEmpty = true});
  curve.creators = {"new curve"};
  curve.create = [curves = a_in.curves,
                  bind = curve.bind](const std::string &) {
    std::vector<RecipeEdit> edits;
    const auto name = UniqueName("curve", curves);
    edits.emplace_back(AddCurve{name});
    if (const auto bound = bind(ReferenceText(name))) {
      edits.push_back(*bound);
    }
    return edits;
  };
  return curve;
}

[[nodiscard]] FormField
OpacityFieldOf(const Inspector &a_in,
               const std::vector<std::string> &a_signalNames) {
  FormField opacity =
      ValueField({.name = "opacity",
                  .kind = FieldKind::kScalar,
                  .text = a_in.row.opacityText,
                  .names = a_in.scalarSignals,
                  .bind = BindLayerOpacity(a_in.output, a_in.layer),
                  .value = std::nullopt,
                  .detail = DetailWhen(NamesSignal(a_in, a_in.row.opacityText),
                                       FieldDetail::kOpacity)});
  opacity.creators = Creators(kValueCreators);
  opacity.create = [current = a_in.row.opacityText, taken = a_signalNames,
                    bind = opacity.bind](const std::string &a_creator) {
    return CreateValue({.creator = a_creator,
                        .field = "opacity",
                        .current = current,
                        .colour = false,
                        .taken = taken,
                        .bind = bind});
  };
  return opacity;
}

[[nodiscard]] FormField
ColourFieldOf(const Inspector &a_in,
              const std::vector<std::string> &a_signalNames) {
  FormField colour =
      ValueField({.name = "colour",
                  .kind = FieldKind::kColor,
                  .text = a_in.row.color,
                  .names = a_in.colorSignals,
                  .bind = BindLayerColor(a_in.output, a_in.layer),
                  .value = std::nullopt,
                  .detail = DetailWhen(NamesSignal(a_in, a_in.row.color),
                                       FieldDetail::kColor),
                  .allowEmpty = true});
  colour.creators = Creators(kValueCreators);
  colour.create = [current = a_in.row.color, taken = a_signalNames,
                   bind = colour.bind](const std::string &a_creator) {
    return CreateValue({.creator = a_creator,
                        .field = "colour",
                        .current = current,
                        .colour = true,
                        .taken = taken,
                        .bind = bind});
  };
  return colour;
}

[[nodiscard]] FormField
MaskFieldOf(const Inspector &a_in,
            const std::vector<std::string> &a_sourceNames) {
  FormField mask = ReferenceField(
      {.name = "mask",
       .text = a_in.row.mask,
       .names = a_in.masks,
       .allowEmpty = true,
       .bind = BindLayerMask(a_in.output, a_in.layer),
       .detail = DetailWhen(a_in.mask.has_value(), FieldDetail::kMask),
       .creators = {"new mask"}});
  mask.create = [taken = a_sourceNames,
                 bind = mask.bind](const std::string &a_creator) {
    return CreateImage(a_creator, taken, bind);
  };
  return mask;
}
}

std::vector<FormField> InspectorForm(const Inspector &a_inspector) {
  const Inspector &in = a_inspector;
  const auto sourceNames = Joined(in.sources, in.masks);
  const auto signalNames = Joined(in.scalarSignals, in.colorSignals);
  std::vector<FormField> form;
  form.push_back(SourceFieldOf(in, sourceNames));
  form.push_back(CurveFieldOf(in));
  form.push_back(OpacityFieldOf(in, signalNames));
  form.push_back(ColourFieldOf(in, signalNames));
  form.push_back(MaskFieldOf(in, sourceNames));
  form.push_back(ValueField({.name = "channels",
                             .kind = FieldKind::kChannels,
                             .text = in.row.channels,
                             .names = {},
                             .bind = BindLayerChannels(in.output, in.layer)}));
  return form;
}

std::vector<FormField> ScalarForm(const LayerStack &a_stack) {
  std::vector<FormField> form;
  form.reserve(a_stack.scalars.size());
  const auto signalNames = Joined(a_stack.scalarSignals, a_stack.colorSignals);
  for (const auto &scalar : a_stack.scalars) {
    const auto field = ParseScalarField(scalar.name);
    const bool colour = field == std::optional{ScalarField::kColor};
    const auto &names = colour ? a_stack.colorSignals : a_stack.scalarSignals;
    const bool signal =
        IsWholeReference(scalar.text) &&
        std::ranges::find(names, ReferenceName(scalar.text)) != names.end();
    FormField row =
        ValueField({.name = scalar.name,
                    .kind = colour ? FieldKind::kColor : FieldKind::kScalar,
                    .text = scalar.text,
                    .names = names,
                    .bind = BindScalar(a_stack.output, field),
                    .value = scalar.value,
                    .detail = signal ? std::optional{FieldDetail::kSignal}
                                     : std::nullopt});
    row.creators = Creators(kValueCreators);
    row.create = [name = scalar.name, current = scalar.text, colour,
                  taken = signalNames,
                  bind = row.bind](const std::string &a_creator) {
      return CreateValue({.creator = a_creator,
                          .field = name,
                          .current = current,
                          .colour = colour,
                          .taken = taken,
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
  a_form.push_back(TextedField({.name = "value",
                                .kind = FieldKind::kSignalValue,
                                .text = text,
                                .bind = BindSignalValue(a_ctx.name)}));
}

void ExprFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                const ExprSignal &a_expr) {
  a_form.push_back(TextedField({.name = "value",
                                .kind = FieldKind::kSignalValue,
                                .text = a_expr.text,
                                .bind = BindSignalValue(a_ctx.name)}));
}

void PulseFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const PulseSignal &a_pulse) {
  a_form.push_back(
      ParamField({.name = "base",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.base),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &PulseSignal::base, ParseParam)}));
  a_form.push_back(ParamField(
      {.name = "amplitude",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_pulse.amplitude),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &PulseSignal::amplitude, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "period",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.period),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &PulseSignal::period, ParseParam)}));
  a_form.push_back(
      ParamField({.name = "phase",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_pulse.phase),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &PulseSignal::phase, ParseParam)}));
  a_form.push_back(ChoiceField(
      "waveform", std::string{NameOf(kWaveforms, a_pulse.waveform)},
      WordsOf(kWaveforms),
      BindSignalMember(a_ctx.name, a_ctx.record, &PulseSignal::waveform,
                       WordOf(kWaveforms))));
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
                                           &RampSignal::seconds, ParseParam)}));
}

void EfshFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                const EfshSignal &a_efsh) {
  a_form.push_back(
      ChoiceField("field", std::string{NameOf(kEfshFields, a_efsh.field)},
                  WordsOf(kEfshFields),
                  BindSignalMember(a_ctx.name, a_ctx.record, &EfshSignal::field,
                                   WordOf(kEfshFields))));
  a_form.push_back(
      TextedField({.name = "record",
                   .kind = FieldKind::kText,
                   .text = a_efsh.record.text,
                   .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                            &EfshSignal::record, FormOf)}));
}

void ActorValueFields(std::vector<FormField> &a_form,
                      const SignalContext &a_ctx,
                      const ActorValueSignal &a_actorValue) {
  a_form.push_back(TextedField(
      {.name = "actorValue",
       .kind = FieldKind::kText,
       .text = a_actorValue.actorValue,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &ActorValueSignal::actorValue, TextOf)}));
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

void TriggerOriginFields(std::vector<FormField> &a_form,
                         const SignalContext &a_ctx,
                         const TriggerSignal &a_trigger) {
  Match(
      a_trigger.origin,
      [&](const EventOrigin &a_event) {
        a_form.push_back(TextedField(
            {.name = "event",
             .kind = FieldKind::kText,
             .text = a_event.event,
             .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                       &EventOrigin::event, TextOf)}));
        a_form.push_back(
            TextedField({.name = "at",
                         .kind = FieldKind::kText,
                         .text = a_event.at,
                         .bind = BindTriggerMember(a_ctx.name, a_ctx.record,
                                                   &EventOrigin::at, StringAny),
                         .allowEmpty = true}));
      },
      [&](const PluginOrigin &a_plugin) {
        a_form.push_back(TextedField(
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
                                &TriggerSignal::lifetime, ParseParam)}));
  FormField max =
      ParamField({.name = "max",
                  .kind = FieldKind::kScalar,
                  .text = std::to_string(a_trigger.max),
                  .names = {},
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &TriggerSignal::max, CountOf)});
  max.range = std::pair{1.0f, 64.0f};
  a_form.push_back(std::move(max));
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
  a_form.push_back(ChoiceField(
      "field", std::string{NameOf(kPayloadFields, a_payload.field)},
      WordsOf(kPayloadFields),
      BindSignalMember(a_ctx.name, a_ctx.record, &PayloadSignal::field,
                       WordOf(kPayloadFields))));
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
                                &AccumulateSignal::decay, ParseParam)}));
}

void NoiseFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const NoiseSignal &a_noise) {
  a_form.push_back(ParamField(
      {.name = "frequency",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_noise.frequency),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &NoiseSignal::frequency, ParseParam)}));
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
                                           &NoiseSignal::seed, CountOf)}));
}

void GradientFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                    const GradientSignal &a_gradient) {
  a_form.push_back(
      ParamField({.name = "t",
                  .kind = FieldKind::kScalar,
                  .text = ParamText(a_gradient.t),
                  .names = a_ctx.names.scalar,
                  .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                           &GradientSignal::t, ParseParam)}));
}

void DeltaFields(std::vector<FormField> &a_form, const SignalContext &a_ctx,
                 const DeltaSignal &a_delta) {
  a_form.push_back(
      ReferenceField({.name = "of",
                      .text = RefText(a_delta.of),
                      .names = a_ctx.all,
                      .allowEmpty = false,
                      .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                               &DeltaSignal::of, RefOf)}));
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
  a_form.push_back(ParamField(
      {.name = "seconds",
       .kind = FieldKind::kScalar,
       .text = ParamText(a_smooth.seconds),
       .names = a_ctx.names.scalar,
       .bind = BindSignalMember(a_ctx.name, a_ctx.record,
                                &SmoothSignal::seconds, ParseParam)}));
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
      [&](const PulseSignal &a_pulse) { PulseFields(form, ctx, a_pulse); },
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
      [&](const DeltaSignal &a_delta) { DeltaFields(form, ctx, a_delta); },
      [&](const SmoothSignal &a_smooth) { SmoothFields(form, ctx, a_smooth); });
  return form;
}

namespace {
struct SourceContext {
  const std::string &name;
  const SourceKind &record;
  const SignalNames &names;
  const SourceRow &source;
};

void ImageFields(std::vector<FormField> &a_form, const SourceContext &a_ctx) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const SourceRow &source = a_ctx.source;
  a_form.push_back(TextedField(
      {.name = "path",
       .kind = FieldKind::kText,
       .text = source.path,
       .bind = BindSourceMember(name, record, &ImageSource::path, StringAny)}));
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
  scroll.allowEmpty = true;
  a_form.push_back(std::move(scroll));
  FormField tile =
      ParamField({.name = "tile",
                  .kind = FieldKind::kVec2,
                  .text = source.tile,
                  .names = a_ctx.names.vec2,
                  .bind = BindSourceMember(name, record, &ImageSource::tile,
                                           OptionalVec2Of)});
  tile.allowEmpty = true;
  a_form.push_back(std::move(tile));
  a_form.push_back(ToggleField("mirrorU", source.mirrorU == "on",
                               BindImageMirror(name, record, 0)));
  a_form.push_back(ToggleField("mirrorV", source.mirrorV == "on",
                               BindImageMirror(name, record, 1)));
  a_form.push_back(ToggleField(
      "transpose", source.transpose == "on",
      BindSourceMember(name, record, &ImageSource::transpose, OnOffAny)));
  a_form.push_back(ParamField(
      {.name = "mip",
       .kind = FieldKind::kScalar,
       .text = source.mip,
       .names = {},
       .bind = BindSourceMember(name, record, &ImageSource::mip, NumberOf)}));
}

void MaterialFields(std::vector<FormField> &a_form,
                    const SourceContext &a_ctx) {
  a_form.push_back(ChoiceField(
      "channel", a_ctx.source.material, WordsOf(kMaterialChannels),
      BindSourceMember(a_ctx.name, a_ctx.record, &MaterialSource::channel,
                       ParseMaterialChannel)));
}

void BakeFields(std::vector<FormField> &a_form, const SourceContext &a_ctx) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  a_form.push_back(ChoiceField(
      "bake", a_ctx.source.bake, WordsOf(kBakeKindWords),
      BindSourceMember(name, record, &BakeSource::bake, DefaultBakeKind)));
  if (a_ctx.source.bake == "partition") {
    a_form.push_back(ChoiceField("partition", a_ctx.source.partition,
                                 BipedSlotNames(),
                                 BindBakePartition(name, record)));
  } else if (a_ctx.source.bake == "boneWeight") {
    a_form.push_back(TextedField({.name = "bones",
                                  .kind = FieldKind::kText,
                                  .text = a_ctx.source.bones,
                                  .bind = BindBakeBones(name, record)}));
  }
}

void UvFields(std::vector<FormField> &a_form, const SourceContext &a_ctx) {
  a_form.push_back(ChoiceField("axis", a_ctx.source.axis, WordsOf(kUvAxes),
                               BindSourceMember(a_ctx.name, a_ctx.record,
                                                &UvSource::axis, ParseUvAxis)));
}

void DistanceFields(std::vector<FormField> &a_form,
                    const SourceContext &a_ctx) {
  a_form.push_back(TextedField(
      {.name = "from",
       .kind = FieldKind::kText,
       .text = a_ctx.source.from,
       .bind = BindSourceMember(a_ctx.name, a_ctx.record, &DistanceSource::from,
                                DistanceFromOf)}));
}

void RippleFields(std::vector<FormField> &a_form, const SourceContext &a_ctx) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const SourceRow &source = a_ctx.source;
  a_form.push_back(ReferenceField(
      {.name = "trigger",
       .text = source.trigger,
       .names = a_ctx.names.triggers,
       .allowEmpty = false,
       .bind = BindSourceMember(name, record, &RippleSource::trigger, RefOf)}));
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

void ClustersFields(std::vector<FormField> &a_form,
                    const SourceContext &a_ctx) {
  const std::string &name = a_ctx.name;
  const SourceKind &record = a_ctx.record;
  const SourceRow &source = a_ctx.source;
  FormField clusters = ParamField(
      {.name = "clusters",
       .kind = FieldKind::kScalar,
       .text = source.clusters,
       .names = {},
       .bind = BindSourceMember(name, record, &MaterialClustersSource::clusters,
                                ClusterCountOf)});
  clusters.range = std::pair{1.0f, static_cast<float>(kMaxMaterialClusters)};
  a_form.push_back(std::move(clusters));
  a_form.push_back(TextedField({.name = "weights",
                                .kind = FieldKind::kText,
                                .text = source.weights,
                                .bind = BindClusterWeights(name, record)}));
  a_form.push_back(
      ParamField({.name = "seed",
                  .kind = FieldKind::kScalar,
                  .text = source.seed,
                  .names = {},
                  .bind = BindSourceMember(
                      name, record, &MaterialClustersSource::seed, SeedOf)}));
  FormField iterations = ParamField(
      {.name = "iterations",
       .kind = FieldKind::kScalar,
       .text = source.iterations,
       .names = {},
       .bind = BindSourceMember(
           name, record, &MaterialClustersSource::iterations, IterationsOf)});
  iterations.range = std::pair{1.0f, static_cast<float>(kMaxClusterIterations)};
  a_form.push_back(std::move(iterations));
}
}

std::vector<FormField> SourceForm(const SourceRow &a_source,
                                  const SignalNames &a_names) {
  std::vector<FormField> form;
  const std::string &name = a_source.name;
  form.push_back(ChoiceField("kind", a_source.kind, WordsOf(kSourceKindWords),
                             BindSourceKindChoice(name)));
  const std::optional<SourceKind> record = SourceKindOf(a_source);
  if (!record) {
    return form;
  }
  const SourceContext ctx{name, *record, a_names, a_source};
  if (a_source.kind == "image") {
    ImageFields(form, ctx);
  } else if (a_source.kind == "material") {
    MaterialFields(form, ctx);
  } else if (a_source.kind == "bake") {
    BakeFields(form, ctx);
  } else if (a_source.kind == "uv") {
    UvFields(form, ctx);
  } else if (a_source.kind == "distance") {
    DistanceFields(form, ctx);
  } else if (a_source.kind == "ripple") {
    RippleFields(form, ctx);
  } else if (a_source.kind == "materialClusters") {
    ClustersFields(form, ctx);
  }
  return form;
}

std::vector<FormField> RecipeHeaderForm(const RecipeRow &a_recipe) {
  std::vector<FormField> form;
  form.push_back(TextedField({.name = "priority",
                              .kind = FieldKind::kText,
                              .text = std::to_string(a_recipe.priority),
                              .bind = BindPriority(),
                              .allowEmpty = true}));
  form.push_back(TextedField({.name = "clockSpeed",
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
                  .bind = BindLightParam(output, LightParam::kIntensity)}));
  a_form.push_back(
      ParamField({.name = "size",
                  .kind = FieldKind::kScalar,
                  .text = a_light.size,
                  .names = a_names.scalar,
                  .bind = BindLightParam(output, LightParam::kSize)}));
  a_form.push_back(
      ParamField({.name = "cutoff",
                  .kind = FieldKind::kScalar,
                  .text = a_light.cutoff,
                  .names = a_names.scalar,
                  .bind = BindLightParam(output, LightParam::kCutoff)}));
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
    a_form.push_back(std::move(max));
    FormField minShare = ValueField(
        {.name = "minShare",
         .kind = FieldKind::kScalar,
         .text = a_light.bonesMinShare,
         .names = {},
         .bind = BindLightSkinnedMinShare(output, a_light.bonesMax)});
    minShare.range = std::pair{0.0f, 1.0f};
    a_form.push_back(std::move(minShare));
  } else {
    a_form.push_back(TextedField({.name = "names",
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
  a_form.push_back(ValueField({.name = "alphaTest",
                               .kind = FieldKind::kScalar,
                               .text = ParamText(a_shell.alphaTest),
                               .names = {},
                               .bind = BindShellAlphaTest()}));
  a_form.push_back(ParamField({.name = "alpha",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.alpha,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kAlpha)}));
  a_form.push_back(ParamField({.name = "rimPower",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.rimPower,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kRimPower)}));
  a_form.push_back(ParamField({.name = "emissive",
                               .kind = FieldKind::kScalar,
                               .text = a_shell.emissive,
                               .names = a_names.scalar,
                               .bind = BindShellParam(ShellParam::kEmissive)}));
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
                               .bind = BindShellParam(ShellParam::kScale)}));
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

std::optional<Vec3> LiteralColor(std::string_view a_text) {
  const auto param = ParseVec3Param(a_text);
  const auto *parts = param ? Get<std::array<Param, 3>>(*param) : nullptr;
  if (parts == nullptr) {
    return std::nullopt;
  }
  const float *x = Get<float>((*parts)[0]);
  const float *y = Get<float>((*parts)[1]);
  const float *z = Get<float>((*parts)[2]);
  if (x == nullptr || y == nullptr || z == nullptr) {
    return std::nullopt;
  }
  return Vec3{*x, *y, *z};
}

std::string LiteralColorText(const Vec3 &a_color) {
  return Vec3ParamText(std::array<Param, 3>{a_color.x, a_color.y, a_color.z});
}
}
