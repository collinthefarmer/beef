#include "recipe/Recipe.h"

#include "recipe/Expression.h"
#include "recipe/Words.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::string_view Trim(std::string_view a_text) noexcept {
  while (!a_text.empty() &&
         std::isspace(static_cast<unsigned char>(a_text.front()))) {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() &&
         std::isspace(static_cast<unsigned char>(a_text.back()))) {
    a_text.remove_suffix(1);
  }
  return a_text;
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b) noexcept {
  return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

std::string TrimCopy(std::string_view a_text) {
  const auto begin = a_text.find_first_not_of(" \t");
  const auto end = a_text.find_last_not_of(" \t");
  return begin == std::string_view::npos
             ? std::string{}
             : std::string{a_text.substr(begin, end - begin + 1)};
}

std::string NumberText(float a_value) {
  auto text = std::format("{:.4f}", a_value);
  while (text.ends_with('0')) {
    text.pop_back();
  }
  if (text.ends_with('.')) {
    text.pop_back();
  }
  return text;
}

std::optional<float> ParseNumber(std::string_view a_text) {
  const auto text = TrimCopy(a_text);
  float value = 0.0f;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size()
             ? std::optional{value}
             : std::nullopt;
}

std::vector<std::string> SplitCommas(std::string_view a_text) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= a_text.size()) {
    const auto comma = a_text.find(',', start);
    parts.push_back(TrimCopy(a_text.substr(
        start, comma == std::string_view::npos ? std::string_view::npos
                                               : comma - start)));
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return parts;
}

constexpr Slot kEverySlot[]{Slot::kDiffuse, Slot::kEmissive, Slot::kRmaos,
                            Slot::kNormal,  Slot::kHeight,   Slot::kFuzz,
                            Slot::kGlint,   Slot::kCoat,     Slot::kSubsurface};
static_assert(std::size(kEverySlot) == kSlotCount);
constexpr Slot kVanillaShellSlots[]{Slot::kEmissive};

bool Contains(std::span<const Slot> a_slots, Slot a_slot) noexcept {
  return std::ranges::find(a_slots, a_slot) != a_slots.end();
}
}

std::optional<FormKey> FormKey::Parse(std::string_view a_text) {
  a_text = Trim(a_text);
  if (a_text.size() < 4 || a_text[0] != '0' ||
      (a_text[1] != 'x' && a_text[1] != 'X')) {
    return std::nullopt;
  }
  const auto sep = a_text.find('~');
  if (sep == std::string_view::npos || sep <= 2 || sep + 1 >= a_text.size()) {
    return std::nullopt;
  }
  const auto hex = a_text.substr(2, sep - 2);
  std::uint32_t id = 0;
  const auto r = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
  if (r.ec != std::errc{} || r.ptr != hex.data() + hex.size() ||
      hex.size() > 8) {
    return std::nullopt;
  }
  return FormKey{std::string{a_text.substr(sep + 1)}, id};
}

std::string FormKey::ToString() const {
  return std::format("0x{:X}~{}", localId, file);
}

bool FormKey::operator==(const FormKey &a_other) const noexcept {
  return localId == a_other.localId && EqualsIgnoringCase(file, a_other.file);
}

FormRef FormRef::From(std::string_view a_text) {
  FormRef ref;
  ref.text = std::string{Trim(a_text)};
  ref.key = FormKey::Parse(ref.text);
  return ref;
}

std::optional<std::string> CurveRef::Named() const {
  if (text.size() > 1 && text[0] == '@' &&
      IsName(std::string_view{text}.substr(1))) {
    return text.substr(1);
  }
  return std::nullopt;
}

std::string_view KeyKindName(KeyKind a_kind) noexcept {
  return NameOf(kKeyKinds, a_kind);
}

KeyOperand KeyOperandOf(KeyKind a_kind) noexcept {
  const auto *row = RowOf(kKeyKinds, a_kind);
  return row ? row->operand : KeyOperand::kForm;
}

int DefaultPriority(KeyKind a_kind) noexcept {
  const auto *row = RowOf(kKeyKinds, a_kind);
  return row ? row->priority : 0;
}

bool EnchantmentDerived(KeyKind a_kind) noexcept {
  const auto *row = RowOf(kKeyKinds, a_kind);
  return row && row->enchantmentDerived;
}

std::string_view RecipeKey::Glob() const noexcept {
  const auto *glob = Get<std::string>(operand);
  return glob ? std::string_view{*glob} : std::string_view{};
}

std::string RecipeKey::ToString() const {
  return Match(
      operand,
      [&](const std::monostate &) { return std::string{KeyKindName(kind)}; },
      [&](const FormRef &form) {
        return std::format("{}:{}", KeyKindName(kind), form.text);
      },
      [&](const std::string &glob) {
        return std::format("{}:{}", KeyKindName(kind), glob);
      });
}

std::string_view SelectorClause::Glob() const noexcept {
  const auto *glob = Get<std::string>(operand);
  return glob ? std::string_view{*glob} : std::string_view{};
}

std::optional<std::uint32_t>
BipedSlotFromName(std::string_view a_name) noexcept {
  for (const auto &e : kBipedSlots) {
    if (EqualsIgnoringCase(e.name, a_name)) {
      return e.slot;
    }
  }
  if (!a_name.empty() && a_name.size() <= 2 &&
      std::ranges::all_of(a_name,
                          [](char c) { return c >= '0' && c <= '9'; })) {
    const std::uint32_t slot =
        static_cast<std::uint32_t>(std::stoul(std::string{a_name}));
    if (slot >= kFirstBipedSlot && slot <= kLastBipedSlot) {
      return slot;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> BipedSlotName(std::uint32_t a_slot) noexcept {
  for (const auto &e : kBipedSlots) {
    if (e.slot == a_slot) {
      return e.name;
    }
  }
  return std::nullopt;
}

SignalKindId SignalKindOf(const SignalKind &a_kind) noexcept {
  static_assert(std::variant_size_v<SignalKind> == kSignalKindCount);
  const std::size_t index = a_kind.index();
  return index < kSignalKindCount ? static_cast<SignalKindId>(index)
                                  : SignalKindId::kConstant;
}

std::string_view SignalKindName(SignalKindId a_kind) noexcept {
  return NameOf(kSignalKinds, a_kind);
}

std::optional<SignalKindId> ParseSignalKind(std::string_view a_name) noexcept {
  return FromName(kSignalKinds, a_name);
}

bool SignalKindTunable(SignalKindId a_kind) noexcept {
  const auto *row = RowOf(kSignalKinds, a_kind);
  return row && row->tunable;
}

std::optional<SignalKind> DefaultSignalKind(std::string_view a_name) {
  const auto id = ParseSignalKind(a_name);
  return id ? AlternativeAt<SignalKind>(static_cast<std::size_t>(*id))
            : std::nullopt;
}

std::string_view SurfaceName(Surface a_surface) noexcept {
  return NameOf(kSurfaces, a_surface);
}

std::optional<Surface> ParseSurface(std::string_view a_name) noexcept {
  return FromName(kSurfaces, a_name);
}

std::string_view TargetName(Target a_target) noexcept {
  return a_target == Target::kLight ? "light"
                                    : SurfaceName(SurfaceOf(a_target));
}

Surface SurfaceOf(Target a_target) noexcept {
  return a_target == Target::kShell ? Surface::kShell : Surface::kMaterial;
}

Target TargetOf(Surface a_surface) noexcept {
  return a_surface == Surface::kShell ? Target::kShell : Target::kMaterial;
}

std::string_view SlotName(Slot a_slot) noexcept {
  return NameOf(kSlots, a_slot);
}

ValueType SourceType(const Source &a_source) noexcept {
  return Match(
      a_source.kind,
      [](const ImageSource &s) {
        return ShaderChannelOf(s.channel) == ShaderChannel::kRgb
                   ? ValueType::kVec3
                   : ValueType::kScalar;
      },
      [](const MaterialSource &s) { return MaterialChannelType(s.channel); },
      [](const BakeSource &s) {
        return Is<PositionBake>(s.bake) || Is<LocalPositionBake>(s.bake)
                   ? ValueType::kVec3
                   : ValueType::kScalar;
      },
      [](const UvSource &) { return ValueType::kScalar; },
      [](const DistanceSource &) { return ValueType::kScalar; },
      [](const RippleSource &) { return ValueType::kScalar; },
      [](const MaterialClustersSource &) { return ValueType::kScalar; });
}

bool IsName(std::string_view a_text) noexcept {
  if (a_text.empty() || (!std::isalpha(static_cast<unsigned char>(a_text[0])) &&
                         a_text[0] != '_')) {
    return false;
  }
  return std::ranges::all_of(a_text, [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  });
}

std::string ParamText(const Param &a_param) {
  return Match(
      a_param, [](float f) { return NumberText(f); },
      [](const Ref &r) { return "@" + r.name; });
}

std::optional<Param> ParseParam(std::string_view a_text) {
  const auto text = TrimCopy(a_text);
  if (text.starts_with('@') && text.size() > 1) {
    return Param{Ref{text.substr(1)}};
  }
  if (const auto number = ParseNumber(text)) {
    return Param{*number};
  }
  return std::nullopt;
}

std::string Vec3ParamText(const Vec3Param &a_param) {
  return Match(
      a_param, [](const Ref &r) { return "@" + r.name; },
      [](const std::array<Param, 3> &parts) {
        return ParamText(parts[0]) + ", " + ParamText(parts[1]) + ", " +
               ParamText(parts[2]);
      });
}

std::string Vec2ParamText(const Vec2Param &a_param) {
  return Match(
      a_param, [](const Ref &r) { return "@" + r.name; },
      [](const std::array<Param, 2> &parts) {
        return ParamText(parts[0]) + ", " + ParamText(parts[1]);
      });
}

std::optional<Vec2Param> ParseVec2Param(std::string_view a_text) {
  const auto text = TrimCopy(a_text);
  if (text.starts_with('@') && text.size() > 1 &&
      text.find(',') == std::string::npos) {
    return Vec2Param{Ref{text.substr(1)}};
  }
  const auto parts = SplitCommas(text);
  if (parts.size() == 1) {
    const auto single = ParseParam(parts[0]);
    if (single && Get<float>(*single)) {
      return Vec2Param{std::array<Param, 2>{*single, *single}};
    }
    return std::nullopt;
  }
  if (parts.size() != 2) {
    return std::nullopt;
  }
  std::array<Param, 2> out;
  for (std::size_t i = 0; i < 2; ++i) {
    const auto part = ParseParam(parts[i]);
    if (!part) {
      return std::nullopt;
    }
    out[i] = *part;
  }
  return Vec2Param{out};
}

std::optional<Vec3Param> ParseVec3Param(std::string_view a_text) {
  const auto text = TrimCopy(a_text);
  if (text.starts_with('@') && text.size() > 1 &&
      text.find(',') == std::string::npos) {
    return Vec3Param{Ref{text.substr(1)}};
  }
  const auto parts = SplitCommas(text);
  if (parts.size() == 1) {
    const auto single = ParseParam(parts[0]);
    if (single && Get<float>(*single)) {
      return Vec3Param{std::array<Param, 3>{*single, *single, *single}};
    }
    return std::nullopt;
  }
  if (parts.size() != 3) {
    return std::nullopt;
  }
  std::array<Param, 3> out;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto part = ParseParam(parts[i]);
    if (!part) {
      return std::nullopt;
    }
    out[i] = *part;
  }
  return Vec3Param{out};
}

void NormaliseColor(std::array<Param, 3> &a_parts) noexcept {
  float largest = 0.0f;
  for (const auto &part : a_parts) {
    const auto *number = Get<float>(part);
    if (!number) {
      return;
    }
    largest = (std::max)(largest, *number);
  }
  if (largest > 1.0f) {
    for (auto &part : a_parts) {
      part = *Get<float>(part) / 255.0f;
    }
  }
}

std::optional<Vec3Param> ParseColorParam(std::string_view a_text) {
  auto param = ParseVec3Param(a_text);
  if (param) {
    if (auto *parts = Get<std::array<Param, 3>>(*param)) {
      NormaliseColor(*parts);
    }
  }
  return param;
}

std::string_view BlendName(Blend a_blend) noexcept {
  return NameOf(kBlends, a_blend);
}

std::optional<Blend> ParseBlend(std::string_view a_name) noexcept {
  return FromName(kBlends, a_name);
}

std::uint32_t BlendShaderMode(Blend a_blend) noexcept {
  const auto *row = RowOf(kBlends, a_blend);
  return row ? row->shaderMode : 0;
}

std::string LayerSourceText(const LayerSource &a_source) {
  return Match(
      a_source, [](const Ref &r) { return "@" + r.name; },
      [](const Vec3 &v) {
        return NumberText(v.x) + ", " + NumberText(v.y) + ", " +
               NumberText(v.z);
      });
}

std::optional<LayerSource> ParseLayerSource(std::string_view a_text) {
  const auto text = TrimCopy(a_text);
  if (text.starts_with('@') && text.size() > 1) {
    return LayerSource{Ref{text.substr(1)}};
  }
  const auto parts = SplitCommas(text);
  if (parts.size() == 1) {
    const auto single = ParseNumber(parts[0]);
    return single ? std::optional{LayerSource{Vec3{*single, *single, *single}}}
                  : std::nullopt;
  }
  if (parts.size() != 3) {
    return std::nullopt;
  }
  const auto x = ParseNumber(parts[0]), y = ParseNumber(parts[1]),
             z = ParseNumber(parts[2]);
  if (!x || !y || !z) {
    return std::nullopt;
  }
  std::array<Param, 3> colour{*x, *y, *z};
  NormaliseColor(colour);
  return LayerSource{Vec3{*Get<float>(colour[0]), *Get<float>(colour[1]),
                          *Get<float>(colour[2])}};
}

std::string_view MaterialChannelName(MaterialChannel a_channel) noexcept {
  return NameOf(kMaterialChannels, a_channel);
}

std::optional<MaterialChannel>
ParseMaterialChannel(std::string_view a_name) noexcept {
  return FromName(kMaterialChannels, a_name);
}

std::string_view ImageChannelName(ImageChannel a_channel) noexcept {
  return NameOf(kImageChannels, a_channel);
}

std::optional<ImageChannel>
ParseImageChannel(std::string_view a_name) noexcept {
  return FromName(kImageChannels, a_name);
}

std::string_view ImageSpaceName(ImageSpace a_space) noexcept {
  return NameOf(kImageSpaces, a_space);
}

std::optional<ImageSpace> ParseImageSpace(std::string_view a_name) noexcept {
  return FromName(kImageSpaces, a_name);
}

std::string_view UvAxisName(UvAxis a_axis) noexcept {
  return NameOf(kUvAxes, a_axis);
}

std::optional<UvAxis> ParseUvAxis(std::string_view a_name) noexcept {
  return FromName(kUvAxes, a_name);
}

std::string_view RippleShapeName(RippleShape a_shape) noexcept {
  return NameOf(kRippleShapes, a_shape);
}

std::optional<RippleShape> ParseRippleShape(std::string_view a_name) noexcept {
  return FromName(kRippleShapes, a_name);
}

std::string_view ShellMaterialName(ShellMaterial a_material) noexcept {
  return NameOf(kShellMaterials, a_material);
}

std::optional<ShellMaterial>
ParseShellMaterial(std::string_view a_name) noexcept {
  return FromName(kShellMaterials, a_name);
}

std::string_view ShellBlendName(ShellBlend a_blend) noexcept {
  return NameOf(kShellBlends, a_blend);
}

std::optional<ShellBlend> ParseShellBlend(std::string_view a_name) noexcept {
  return FromName(kShellBlends, a_name);
}

std::string_view ScalarFieldName(ScalarField a_field) noexcept {
  return NameOf(kScalarFields, a_field);
}

std::optional<ScalarField> ParseScalarField(std::string_view a_name) noexcept {
  return FromName(kScalarFields, a_name);
}

float ScalarFallback(ScalarField a_field) noexcept {
  const auto *row = RowOf(kScalarFields, a_field);
  return row ? row->fallback : 0.0f;
}

std::optional<Param> *ScalarOf(SlotScalars &a_scalars,
                               ScalarField a_field) noexcept {
  const auto *row = RowOf(kScalarFields, a_field);
  if (!row) {
    return nullptr;
  }
  const auto *member = Get<std::optional<Param> SlotScalars::*>(row->member);
  return member ? &(a_scalars.**member) : nullptr;
}

const std::optional<Param> *ScalarOf(const SlotScalars &a_scalars,
                                     ScalarField a_field) noexcept {
  return ScalarOf(const_cast<SlotScalars &>(a_scalars), a_field);
}

MaterialMap BaseMapOf(Slot a_slot) noexcept {
  const auto *row = RowOf(kSlots, a_slot);
  return row ? row->baseMap : MaterialMap::kNone;
}

ShaderChannel ShaderChannelOf(ImageChannel a_channel) noexcept {
  const auto *row = RowOf(kImageChannels, a_channel);
  return row ? row->channel : ShaderChannel::kRgb;
}

MaterialMap MaterialMapOf(MaterialChannel a_channel) noexcept {
  const auto *row = RowOf(kMaterialChannels, a_channel);
  return row ? row->map : MaterialMap::kNone;
}

ShaderChannel ShaderChannelOf(MaterialChannel a_channel) noexcept {
  const auto *row = RowOf(kMaterialChannels, a_channel);
  return row ? row->channel : ShaderChannel::kR;
}

ValueType MaterialChannelType(MaterialChannel a_channel) noexcept {
  const auto *row = RowOf(kMaterialChannels, a_channel);
  return row ? row->type : ValueType::kScalar;
}

bool Thresholdable(MaterialChannel a_channel) noexcept {
  return RowOf(kMaterialChannels, a_channel) &&
         MaterialChannelType(a_channel) == ValueType::kScalar;
}

std::span<const Slot> SlotsOf(Surface a_surface,
                              ShellMaterial a_shell) noexcept {
  if (a_surface == Surface::kShell && a_shell == ShellMaterial::kVanilla) {
    return kVanillaShellSlots;
  }
  return kEverySlot;
}

bool SurfaceHasSlot(Surface a_surface, ShellMaterial a_shell,
                    Slot a_slot) noexcept {
  return Contains(SlotsOf(a_surface, a_shell), a_slot);
}

std::span<const ScalarField> ScalarsOf(Slot a_slot) noexcept {
  const auto *row = RowOf(kSlots, a_slot);
  return row ? row->scalars : std::span<const ScalarField>{};
}

bool ScalarRequired(Slot a_slot, ScalarField a_field) noexcept {
  const auto *row = RowOf(kSlots, a_slot);
  return row && row->scalarsRequired &&
         std::ranges::find(row->scalars, a_field) != row->scalars.end();
}

bool SlotsExclude(Slot a_first, Slot a_second) noexcept {
  const auto *row = RowOf(kSlots, a_first);
  return row && Contains(row->excludes, a_second);
}

bool BlendAllowed(Slot a_slot, Blend a_blend) noexcept {
  const auto *row = RowOf(kBlends, a_blend);
  return row && (!row->normalStackOnly || a_slot == Slot::kNormal);
}

ChannelSet ChannelsOf(Slot a_slot) noexcept {
  const auto *row = RowOf(kSlots, a_slot);
  return row ? row->channels : ChannelSet{};
}

std::string_view SlotChannelNote(Slot a_slot) noexcept {
  const auto *row = RowOf(kSlots, a_slot);
  return row ? row->note : "";
}

std::optional<ChannelSet> ChannelSet::Parse(std::string_view a_text) {
  ChannelSet set{false, false, false, false};
  if (a_text.empty() || a_text.size() > 4) {
    return std::nullopt;
  }
  for (const char c : a_text) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
    case 'r':
      set.r = true;
      break;
    case 'g':
      set.g = true;
      break;
    case 'b':
      set.b = true;
      break;
    case 'a':
      set.a = true;
      break;
    default:
      return std::nullopt;
    }
  }
  return set;
}

std::string ChannelSet::ToString() const {
  std::string out;
  if (r)
    out += 'r';
  if (g)
    out += 'g';
  if (b)
    out += 'b';
  if (a)
    out += 'a';
  return out;
}

std::optional<SourceKind> DefaultSourceKind(std::string_view a_name) {
  const std::optional<SourceKindId> id = FromName(kSourceKindWords, a_name);
  if (!id) {
    return std::nullopt;
  }
  return AlternativeAt<SourceKind>(static_cast<std::size_t>(*id));
}

std::optional<BakeKind> DefaultBakeKind(std::string_view a_name) {
  for (std::size_t i = 0; i < std::size(kBakeKindWords); ++i) {
    if (kBakeKindWords[i] == a_name) {
      return AlternativeAt<BakeKind>(i);
    }
  }
  return std::nullopt;
}

std::string_view SourceKindName(const SourceKind &a_kind) noexcept {
  return NameOf(kSourceKindWords, SourceKindIdOf(a_kind));
}

std::string_view BakeKindName(const BakeKind &a_bake) noexcept {
  const std::size_t index = a_bake.index();
  return index < std::size(kBakeKindWords) ? kBakeKindWords[index]
                                           : std::string_view{"?"};
}

namespace {
std::optional<std::string_view> RefOf(const Param &a_param) noexcept {
  const auto *ref = Get<Ref>(a_param);
  return ref ? std::optional<std::string_view>{ref->name} : std::nullopt;
}

template <std::size_t N>
void CollectRefs(const std::variant<std::array<Param, N>, Ref> &a_param,
                 std::vector<std::string_view> &a_out) {
  Match(
      a_param, [&](const Ref &r) { a_out.push_back(r.name); },
      [&](const std::array<Param, N> &parts) {
        for (const auto &p : parts) {
          if (const auto r = RefOf(p)) {
            a_out.push_back(*r);
          }
        }
      });
}

class AnimationQuery {
public:
  explicit AnimationQuery(const Recipe &a_recipe) : recipe_(a_recipe) {}

  bool Signal(std::string_view a_name) {
    const auto *signal = recipe_.FindSignal(a_name);
    if (!signal) {
      return false;
    }
    return Guarded("s:" + std::string{a_name}, [&] {
      return Match(
          signal->kind, [](const ConstantSignal &) { return false; },
          [&](const ExprSignal &e) {
            const auto program = Program::Parse(e.text);
            if (!program) {
              return false;
            }
            if (program->UsesTime()) {
              return true;
            }
            return std::ranges::any_of(
                program->References(),
                [&](const std::string &r) { return Signal(r); });
          },
          [&](const GradientSignal &g) {
            bool any = Param(g.t);
            for (const auto &stop : g.stops) {
              any = any || Vector(stop.color);
            }
            return any;
          },
          [&](const DeltaSignal &d) { return Signal(d.of.name); },
          [&](const SmoothSignal &s) { return Signal(s.of.name); },
          [](const PulseSignal &) { return true; },
          [](const RampSignal &) { return true; },
          [](const EfshSignal &) { return true; },
          [](const ActorValueSignal &) { return true; },
          [](const ActorStateSignal &) { return true; },
          [](const EnchantmentSignal &) { return true; },
          [](const TriggerSignal &) { return true; },
          [](const PayloadSignal &) { return true; },
          [](const CounterSignal &) { return true; },
          [](const AccumulateSignal &) { return true; },
          [](const NoiseSignal &) { return true; });
    });
  }

  bool Param(const BetterEnchantmentEffects::Param &a_param) {
    const auto name = RefOf(a_param);
    return name && Signal(*name);
  }

  template <std::size_t N>
  bool Vector(const std::variant<std::array<BetterEnchantmentEffects::Param, N>,
                                 Ref> &a_param) {
    std::vector<std::string_view> refs;
    CollectRefs(a_param, refs);
    return std::ranges::any_of(refs,
                               [&](std::string_view r) { return Signal(r); });
  }

  bool Source(std::string_view a_name) {
    const auto *source = recipe_.FindSource(a_name);
    if (!source) {
      return false;
    }
    return Guarded("r:" + std::string{a_name}, [&] {
      return Match(
          source->kind,
          [&](const ImageSource &s) {
            return (s.scroll && Vector(*s.scroll)) ||
                   (s.tile && Vector(*s.tile));
          },
          [](const RippleSource &) { return true; },
          [](const MaterialSource &) { return false; },
          [](const BakeSource &) { return false; },
          [](const UvSource &) { return false; },
          [](const DistanceSource &) { return false; },
          [](const MaterialClustersSource &) { return false; });
    });
  }

  bool Mask(std::string_view a_name) {
    const auto *mask = recipe_.FindMask(a_name);
    if (!mask) {
      return false;
    }
    return Guarded("m:" + std::string{a_name}, [&] {
      const auto program = Program::Parse(mask->text);
      if (!program) {
        return false;
      }
      if (program->UsesTime()) {
        return true;
      }
      return std::ranges::any_of(
          program->References(),
          [&](const std::string &r) { return Image(r); });
    });
  }

  bool Image(std::string_view a_name) {
    if (recipe_.FindSource(a_name)) {
      return Source(a_name);
    }
    if (recipe_.FindMask(a_name)) {
      return Mask(a_name);
    }
    return Signal(a_name);
  }

private:
  template <class F> bool Guarded(const std::string &a_key, F a_f) {
    if (!visiting_.insert(a_key).second) {
      return false;
    }
    const bool result = a_f();
    visiting_.erase(a_key);
    return result;
  }

  const Recipe &recipe_;
  std::unordered_set<std::string> visiting_;
};
}

bool IsAnimated(const Recipe &a_recipe, std::string_view a_signal) {
  return AnimationQuery{a_recipe}.Signal(a_signal);
}

bool IsAnimated(const Recipe &a_recipe, const Source &a_source) {
  return AnimationQuery{a_recipe}.Source(a_source.name);
}

bool IsAnimated(const Recipe &a_recipe, const Mask &a_mask) {
  return AnimationQuery{a_recipe}.Mask(a_mask.name);
}

bool IsAnimated(const Recipe &a_recipe, const Output &a_output) {
  AnimationQuery q{a_recipe};
  return Match(
      a_output,
      [&](const LightOutput &l) {
        return q.Vector(l.color) || q.Param(l.intensity) || q.Param(l.size) ||
               q.Param(l.cutoff) || q.Vector(l.offset);
      },
      [&](const SurfaceOutput &m) {
        for (const auto &l : m.stack) {
          if (const auto *ref = Get<Ref>(l.source); ref && q.Image(ref->name)) {
            return true;
          }
          if (q.Param(l.opacity) || (l.color && q.Vector(*l.color)) ||
              (l.mask && q.Mask(l.mask->name))) {
            return true;
          }
        }
        return false;
      });
}
}
