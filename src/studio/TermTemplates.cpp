#include "studio/TermTemplates.h"

#include "Core.h"
#include "recipe/Expression.h"
#include "recipe/Words.h"
#include "studio/Fields.h"
#include "studio/PaintSession.h"
#include "studio/Rows.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
std::vector<RecipeEdit>
ScratchEdits(std::span<const Term> a_terms, std::optional<std::size_t> a_solo,
             const std::set<std::size_t> &a_muted,
             const std::optional<std::string> &a_scratch) {
  std::vector<RecipeEdit> edits;
  std::string text = BuildMask(a_terms, a_solo, a_muted);
  if (text.empty()) {
    text = "0";
  }
  if (!a_scratch) {
    edits.emplace_back(AddMask{std::string{kScratchMask}});
  }
  if (!a_scratch || *a_scratch != text) {
    edits.emplace_back(SetMask{std::string{kScratchMask}, std::move(text)});
  }
  return edits;
}

BuiltTerm MaterialiseTerm(const MaskPreset &a_preset,
                          const SourceCatalog &a_existing) {
  SourcePlanBuilder sources(a_existing);
  std::vector<std::string> factors;
  if (a_preset.partition) {
    factors.push_back(ReferenceText(sources.ReuseOrAdd(
        "partition", BakeSource{PartitionBake{*a_preset.partition}})));
  }
  if (!a_preset.bones.empty()) {
    factors.push_back(ReferenceText(sources.ReuseOrAdd(
        "bones", BakeSource{BoneWeightBake{a_preset.bones}})));
  }
  if (!a_preset.expression.empty()) {
    std::vector<ExpressionRename> renames;
    for (const auto &[wanted, kind] : a_preset.sources) {
      const std::string name = sources.ReuseOrAdd(wanted, kind);
      if (name != wanted) {
        renames.push_back({wanted, name});
      }
    }
    factors.push_back(RenameInExpression(a_preset.expression, renames, false));
  }
  std::string expression;
  for (const std::string &factor : factors) {
    expression += (expression.empty() ? "" : " * ") + factor;
  }
  return BuiltTerm{std::move(sources).TakeEdits(), std::move(expression)};
}

std::string TermLabel(std::string_view a_text, const MaskPresets &a_presets,
                      const SourceCatalog &a_existing) {
  if (a_text.size() > 1 && a_text.front() == '@' && IsName(a_text.substr(1))) {
    return std::string{a_text.substr(1)};
  }
  for (const MaskPreset &preset : a_presets.presets) {
    const BuiltTerm term = MaterialiseTerm(preset, a_existing);
    if (term.edits.empty() && term.expression == a_text) {
      return preset.name;
    }
  }
  return std::string{kExpressionLabel};
}

std::string ProposedMaskName(std::span<const Term> a_terms,
                             std::string_view a_editing) {
  if (!a_editing.empty()) {
    return std::string{a_editing};
  }
  std::string name;
  for (const Term &term : a_terms) {
    if (term.label.empty() || term.label == kExpressionLabel ||
        term.text.empty()) {
      continue;
    }
    std::string part =
        term.label.front() == '@' ? term.label.substr(1) : term.label;
    if (part.empty()) {
      continue;
    }
    if (!name.empty()) {
      part[0] =
          static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
    }
    name += part;
  }
  return IsName(name) ? name : "mask";
}

namespace {
[[nodiscard]] std::vector<MaterialChannel> ThresholdChannels() {
  std::vector<MaterialChannel> channels;
  for (const MaterialChannelSpec &row : kMaterialChannels) {
    if (Thresholdable(row.value)) {
      channels.push_back(row.value);
    }
  }
  return channels;
}

[[nodiscard]] std::vector<std::string> ThresholdChannelNames() {
  std::vector<std::string> names;
  for (const MaterialChannel channel : ThresholdChannels()) {
    names.emplace_back(MaterialChannelName(channel));
  }
  return names;
}

[[nodiscard]] std::string NumberText(float a_value) {
  return ParamText(Param{a_value});
}

[[nodiscard]] std::optional<float> ReadNumber(std::string_view a_text) {
  while (!a_text.empty() && a_text.front() == ' ') {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() && a_text.back() == ' ') {
    a_text.remove_suffix(1);
  }
  float value = 0.0f;
  const auto result =
      std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
  if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() ||
      !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::uint32_t> ReadWhole(std::string_view a_text,
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

[[nodiscard]] float Spelled(float a_value) {
  return ReadNumber(NumberText(a_value)).value_or(a_value);
}

[[nodiscard]] int Percent(float a_share) {
  return static_cast<int>(
      std::lround(std::clamp(a_share, 0.0f, 1.0f) * 100.0f));
}

[[nodiscard]] std::string PartitionName(const GeometryRow &a_geometry,
                                        BipedSlot a_slot) {
  const auto it = std::ranges::find(
      a_geometry.partitions, std::to_underlying(a_slot), &SlotCoverage::slot);
  if (it != a_geometry.partitions.end() && !it->name.empty()) {
    return it->name;
  }
  const auto name = BipedSlotName(a_slot);
  return name ? std::string{*name} : std::to_string(std::to_underlying(a_slot));
}

[[nodiscard]] const MeshIsland *IslandOf(const GeometryRow &a_geometry,
                                         IslandSource a_source,
                                         std::uint16_t a_id) {
  for (const MeshIsland &island : a_geometry.islands) {
    if (island.source == a_source && island.id == a_id) {
      return &island;
    }
  }
  return nullptr;
}

[[nodiscard]] const MaterialCluster *ClusterOf(const GeometryRow &a_geometry,
                                               std::uint8_t a_id) {
  for (const MaterialCluster &cluster : a_geometry.clusters) {
    if (cluster.id == a_id) {
      return &cluster;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string OperandText(const std::string &a_name,
                                      std::uint8_t a_posterize) {
  std::string reference = ReferenceText(a_name);
  if (a_posterize > 1) {
    return std::format("floor({} * {}) / {}", reference, a_posterize,
                       a_posterize);
  }
  return reference;
}

[[nodiscard]] std::string EdgeText(float a_centre, float a_softness,
                                   const std::string &a_operand) {
  const std::string centre = NumberText(a_centre);
  const std::string softness = NumberText(a_softness);
  return std::format("smoothstep({} - {}, {} + {}, {})", centre, softness,
                     centre, softness, a_operand);
}

[[nodiscard]] std::string ThresholdText(const ThresholdTerm &a_term,
                                        const std::string &a_name) {
  const std::string operand = OperandText(a_name, a_term.posterize);
  const bool hasLow = a_term.low != 0.0f;
  const bool hasHigh = a_term.high != 1.0f;
  std::string body;
  if (hasLow && hasHigh) {
    body = std::format("{} * (1 - {})",
                       EdgeText(a_term.low, a_term.softness, operand),
                       EdgeText(a_term.high, a_term.softness, operand));
  } else if (hasLow) {
    body = EdgeText(a_term.low, a_term.softness, operand);
  } else if (hasHigh) {
    body =
        std::format("1 - {}", EdgeText(a_term.high, a_term.softness, operand));
  } else {
    body = std::format("step(0, {})", operand);
  }
  return a_term.invert ? std::format("1 - ({})", body) : body;
}

[[nodiscard]] std::string IdMatchText(const std::string &a_name,
                                      std::uint32_t a_id) {
  return std::format("abs({} * 255 - {}) < 0.5", ReferenceText(a_name), a_id);
}

[[nodiscard]] const MaskPreset *PresetNamed(const MaskPresets &a_presets,
                                            std::string_view a_name) {
  const auto it =
      std::ranges::find(a_presets.presets, a_name, &MaskPreset::name);
  return it == a_presets.presets.end() ? nullptr : &*it;
}
}

BuiltTerm BuildTerm(const TermKind &a_kind, const MaskPresets &a_presets,
                    const SourceCatalog &a_existing) {
  SourcePlanBuilder sources(a_existing);
  std::string text = Match(
      a_kind, [](const RawTerm &) { return std::string{}; },
      [](const ReferenceTerm &t) { return ReferenceText(t.name); },
      [&](const ThresholdTerm &t) {
        const std::string name =
            sources.ReuseOrAdd(std::string{MaterialChannelName(t.channel)},
                               MaterialSource{t.channel});
        return ThresholdText(t, name);
      },
      [&](const PresetTerm &t) {
        const MaskPreset *preset = PresetNamed(a_presets, t.preset);
        if (!preset) {
          return std::string{"0"};
        }
        BuiltTerm term = MaterialiseTerm(*preset, a_existing);
        for (const RecipeEdit &edit : term.edits) {
          if (const auto *add = Get<AddSource>(edit)) {
            (void)sources.ReuseOrAdd(add->name, add->kind);
          }
        }
        return term.expression;
      },
      [&](const PartitionTerm &t) {
        return ReferenceText(
            sources.ReuseOrAdd("partition", BakeSource{PartitionBake{t.slot}}));
      },
      [&](const BoneTerm &t) {
        return ReferenceText(
            sources.ReuseOrAdd("bones", BakeSource{BoneWeightBake{t.bones}}));
      },
      [&](const IslandTerm &t) {
        const std::string name = sources.ReuseOrAdd(
            t.source == IslandSource::kChart ? "charts" : "components",
            IslandBakeOf(t.source));
        return IdMatchText(name, t.id);
      },
      [&](const ClusterTerm &t) {
        const std::string name =
            sources.ReuseOrAdd("clusters", SourceOf(t.settings));
        return IdMatchText(name, t.id);
      });
  return BuiltTerm{std::move(sources).TakeEdits(), std::move(text)};
}

std::string TermLabelOf(const TermKind &a_kind, const MaskPresets &a_presets,
                        const GeometryRow &a_geometry) {
  (void)a_presets;
  return Match(
      a_kind, [](const RawTerm &) { return std::string{kExpressionLabel}; },
      [](const ReferenceTerm &t) { return ReferenceText(t.name); },
      [](const ThresholdTerm &t) {
        return std::format("{} {}..{}", MaterialChannelName(t.channel),
                           NumberText(t.low), NumberText(t.high));
      },
      [](const PresetTerm &t) { return t.preset; },
      [&](const PartitionTerm &t) { return PartitionName(a_geometry, t.slot); },
      [](const BoneTerm &t) {
        std::string label;
        for (const std::string &bone : t.bones) {
          label += (label.empty() ? "" : ", ") + bone;
        }
        return label;
      },
      [&](const IslandTerm &t) {
        const MeshIsland *island = IslandOf(a_geometry, t.source, t.id);
        if (!island) {
          return std::format("{} {}", PlainIslandSourceName(t.source), t.id);
        }
        if (island->dominantBone.empty()) {
          return std::format("{} {}: {}%", PlainIslandSourceName(t.source),
                             t.id, Percent(island->share));
        }
        return std::format("{} {}: {}, {}%", PlainIslandSourceName(t.source),
                           t.id, island->dominantBone, Percent(island->share));
      },
      [&](const ClusterTerm &t) {
        const MaterialCluster *cluster = ClusterOf(a_geometry, t.id);
        if (!cluster) {
          return std::format("material {}", t.id);
        }
        return std::format("material {}: {}, {}%", t.id, cluster->description,
                           Percent(cluster->share));
      });
}

namespace {
template <class Kind>
[[nodiscard]] TermField
Setting(Kind a_kind, FormField a_field,
        std::function<bool(Kind &, const std::string &)> a_set) {
  return TermField{std::move(a_field),
                   [a_kind = std::move(a_kind), a_set = std::move(a_set)](
                       const std::string &a_text) -> std::optional<TermKind> {
                     Kind edited = a_kind;
                     return a_set(edited, a_text)
                                ? std::optional<TermKind>{edited}
                                : std::nullopt;
                   }};
}

[[nodiscard]] std::optional<float> NumberIn(const std::string &a_text,
                                            float a_min, float a_max) {
  const auto number = ReadNumber(a_text);
  if (!number || *number < a_min || *number > a_max) {
    return std::nullopt;
  }
  return Spelled(*number);
}

[[nodiscard]] std::string
BoneListText(const std::vector<std::string> &a_bones) {
  std::string text;
  for (const std::string &bone : a_bones) {
    text += (text.empty() ? "" : ", ") + bone;
  }
  return text;
}

[[nodiscard]] std::optional<std::vector<std::string>>
ParseBoneList(std::string_view a_text) {
  std::vector<std::string> bones;
  while (!a_text.empty()) {
    const auto comma = a_text.find(',');
    auto part = a_text.substr(0, comma);
    a_text = comma == std::string_view::npos ? std::string_view{}
                                             : a_text.substr(comma + 1);
    while (!part.empty() && part.front() == ' ') {
      part.remove_prefix(1);
    }
    while (!part.empty() && part.back() == ' ') {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      bones.emplace_back(part);
    }
  }
  if (bones.empty() || bones.size() > kMaxPresetBones) {
    return std::nullopt;
  }
  return bones;
}

void ThresholdRangeFields(std::vector<TermField> &a_form,
                          const ThresholdTerm &a_term) {
  using Set = std::function<bool(ThresholdTerm &, const std::string &)>;
  a_form.push_back(Setting<ThresholdTerm>(
      a_term,
      TextedField({.name = "low",
                   .kind = FieldKind::kScalar,
                   .text = NumberText(a_term.low),
                   .bind = {}}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        const auto number = NumberIn(a_text, -1.0f, 2.0f);
        return number ? (t.low = *number, true) : false;
      }}));
  a_form.push_back(Setting<ThresholdTerm>(
      a_term,
      TextedField({.name = "high",
                   .kind = FieldKind::kScalar,
                   .text = NumberText(a_term.high),
                   .bind = {}}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        const auto number = NumberIn(a_text, -1.0f, 2.0f);
        return number ? (t.high = *number, true) : false;
      }}));
  a_form.push_back(Setting<ThresholdTerm>(
      a_term,
      TextedField({.name = "softness",
                   .kind = FieldKind::kScalar,
                   .text = NumberText(a_term.softness),
                   .bind = {}}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        const auto number = NumberIn(a_text, 0.0f, 1.0f);
        return number ? (t.softness = *number, true) : false;
      }}));
}

std::vector<TermField> ThresholdForm(const ThresholdTerm &a_term) {
  using Set = std::function<bool(ThresholdTerm &, const std::string &)>;
  std::vector<TermField> form;
  form.push_back(Setting<ThresholdTerm>(
      a_term,
      ChoiceField("channel", std::string{MaterialChannelName(a_term.channel)},
                  ThresholdChannelNames(), {}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        const auto channel = ParseMaterialChannel(a_text);
        if (!channel || !Thresholdable(*channel)) {
          return false;
        }
        t.channel = *channel;
        return true;
      }}));
  ThresholdRangeFields(form, a_term);
  form.push_back(Setting<ThresholdTerm>(
      a_term,
      TextedField({.name = "posterize",
                   .kind = FieldKind::kScalar,
                   .text = std::to_string(a_term.posterize),
                   .bind = {}}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        const auto levels = ReadWhole(a_text, 255);
        return levels ? (t.posterize = static_cast<std::uint8_t>(*levels), true)
                      : false;
      }}));
  form.push_back(Setting<ThresholdTerm>(
      a_term, ToggleField("invert", a_term.invert, {}),
      Set{[](ThresholdTerm &t, const std::string &a_text) {
        if (a_text != "on" && a_text != "off") {
          return false;
        }
        t.invert = a_text == "on";
        return true;
      }}));
  return form;
}

std::vector<TermField> ClusterForm(const ClusterTerm &a_term) {
  using Set = std::function<bool(ClusterTerm &, const std::string &)>;
  std::vector<TermField> form;
  form.push_back(Setting<ClusterTerm>(
      a_term,
      TextedField({.name = "clusters",
                   .kind = FieldKind::kScalar,
                   .text = std::to_string(a_term.settings.clusters),
                   .bind = {}}),
      Set{[](ClusterTerm &t, const std::string &a_text) {
        const auto count = ReadWhole(a_text, kMaxClusters);
        if (!count || *count < 1) {
          return false;
        }
        t.settings.clusters = static_cast<std::uint8_t>(*count);
        return true;
      }}));
  const auto weight = [&](const char *a_name, float ChannelWeights::*a_member) {
    form.push_back(Setting<ClusterTerm>(
        a_term,
        TextedField({.name = a_name,
                     .kind = FieldKind::kScalar,
                     .text = NumberText(a_term.settings.weights.*a_member),
                     .bind = {}}),
        Set{[a_member](ClusterTerm &t, const std::string &a_text) {
          const auto number = NumberIn(a_text, 0.0f, kMaxChannelWeight);
          return number ? (t.settings.weights.*a_member = *number, true)
                        : false;
        }}));
  };
  weight("roughness", &ChannelWeights::roughness);
  weight("metallic", &ChannelWeights::metallic);
  weight("occlusion", &ChannelWeights::occlusion);
  weight("reflectance", &ChannelWeights::reflectance);
  weight("luma", &ChannelWeights::luma);
  form.push_back(Setting<ClusterTerm>(
      a_term,
      TextedField({.name = "seed",
                   .kind = FieldKind::kScalar,
                   .text = std::to_string(a_term.settings.seed),
                   .bind = {}}),
      Set{[](ClusterTerm &t, const std::string &a_text) {
        const auto seed =
            ReadWhole(a_text, std::numeric_limits<std::uint32_t>::max());
        return seed ? (t.settings.seed = *seed, true) : false;
      }}));
  return form;
}

std::vector<TermField> ComponentForm(const IslandTerm &a_term,
                                     const MaskPresets &a_presets,
                                     const GeometryRow &a_geometry) {
  std::vector<std::string> labels;
  std::vector<std::uint16_t> ids;
  for (const MeshIsland &island : a_geometry.islands) {
    if (island.source == a_term.source) {
      labels.push_back(TermLabelOf(IslandTerm{a_term.source, island.id},
                                   a_presets, a_geometry));
      ids.push_back(island.id);
    }
  }
  std::vector<TermField> form;
  form.push_back(Setting<IslandTerm>(
      a_term,
      ChoiceField("id", TermLabelOf(a_term, a_presets, a_geometry), labels, {}),
      std::function<bool(IslandTerm &, const std::string &)>{
          [labels, ids](IslandTerm &t, const std::string &a_text) {
            const auto it = std::ranges::find(labels, a_text);
            if (it != labels.end()) {
              const auto index = static_cast<std::size_t>(it - labels.begin());
              t.id = index < ids.size() ? ids[index] : t.id;
              return index < ids.size();
            }
            const auto id = ReadWhole(a_text, kMaxIslands);
            return id ? (t.id = static_cast<std::uint16_t>(*id), true) : false;
          }}));
  return form;
}

std::vector<TermField> BoneForm(const BoneTerm &a_term) {
  std::vector<TermField> form;
  form.push_back(
      Setting<BoneTerm>(a_term,
                        TextedField({.name = "bones",
                                     .kind = FieldKind::kText,
                                     .text = BoneListText(a_term.bones),
                                     .bind = {}}),
                        std::function<bool(BoneTerm &, const std::string &)>{
                            [](BoneTerm &t, const std::string &a_text) {
                              const auto bones = ParseBoneList(a_text);
                              return bones ? (t.bones = *bones, true) : false;
                            }}));
  return form;
}

std::vector<TermField> PartitionForm(const PartitionTerm &a_term,
                                     const GeometryRow &a_geometry) {
  std::vector<std::string> names;
  std::vector<BipedSlot> slots;
  for (const SlotCoverage &partition : a_geometry.partitions) {
    names.push_back(PartitionName(a_geometry, BipedSlot{partition.slot}));
    slots.push_back(BipedSlot{partition.slot});
  }
  std::vector<TermField> form;
  form.push_back(Setting<PartitionTerm>(
      a_term,
      ChoiceField("slot", PartitionName(a_geometry, a_term.slot), names, {}),
      std::function<bool(PartitionTerm &, const std::string &)>{
          [names, slots](PartitionTerm &t, const std::string &a_text) {
            const auto it = std::ranges::find(names, a_text);
            if (it != names.end()) {
              const auto index = static_cast<std::size_t>(it - names.begin());
              t.slot = index < slots.size() ? slots[index] : t.slot;
              return index < slots.size();
            }
            const auto named = BipedSlotFromName(a_text);
            const auto numbered = ReadWhole(a_text, 61);
            const std::optional<BipedSlot> slot =
                named ? named
                      : (numbered && *numbered >= 30
                             ? std::optional<BipedSlot>{BipedSlot{*numbered}}
                             : std::nullopt);
            return slot ? (t.slot = *slot, true) : false;
          }}));
  return form;
}
}

std::vector<TermField> TermForm(const TermKind &a_kind,
                                const MaskPresets &a_presets,
                                const GeometryRow &a_geometry) {
  return Match(
      a_kind, [](const RawTerm &) { return std::vector<TermField>{}; },
      [](const ReferenceTerm &) { return std::vector<TermField>{}; },
      [](const PresetTerm &) { return std::vector<TermField>{}; },
      [](const ThresholdTerm &t) { return ThresholdForm(t); },
      [](const ClusterTerm &t) { return ClusterForm(t); },
      [&](const IslandTerm &t) {
        return ComponentForm(t, a_presets, a_geometry);
      },
      [](const BoneTerm &t) { return BoneForm(t); },
      [&](const PartitionTerm &t) { return PartitionForm(t, a_geometry); });
}

namespace {
[[nodiscard]] TermOffer Offer(OfferGroup a_group, std::string a_name,
                              std::string a_detail, TermKind a_kind) {
  TermOffer offer;
  offer.group = a_group;
  offer.name = std::move(a_name);
  offer.detail = std::move(a_detail);
  offer.kind = std::move(a_kind);
  return offer;
}

[[nodiscard]] std::string Joined(std::span<const std::string> a_facts) {
  std::string text;
  for (const std::string &fact : a_facts) {
    text += text.empty() ? fact : ", " + fact;
  }
  return text;
}

[[nodiscard]] TermOffer Unavailable(OfferGroup a_group, std::string a_reason,
                                    TermKind a_kind) {
  TermOffer offer = Offer(a_group, std::string{NameOf(kOfferGroups, a_group)},
                          {}, std::move(a_kind));
  offer.unavailable = std::move(a_reason);
  return offer;
}

constexpr std::string_view kMeshUnread = "the mesh has not been read yet";
constexpr std::string_view kNoClusters = "the material has no clusters yet";

void AppendPartOffers(std::vector<TermOffer> &a_offers,
                      const GeometryRow &a_geometry) {
  if (!a_geometry.meshRead) {
    a_offers.push_back(Unavailable(OfferGroup::kParts, std::string{kMeshUnread},
                                   IslandTerm{}));
  }
  for (const MeshIsland &island : a_geometry.islands) {
    const bool chart = island.source == IslandSource::kChart;
    if (chart && island.twin) {
      continue;
    }
    const IslandTerm term{island.source, island.id};
    std::vector<std::string> facts;
    if (!island.dominantBone.empty()) {
      facts.push_back(std::format("{} {}%", island.dominantBone,
                                  Percent(island.dominantShare)));
    }
    if (island.twin) {
      facts.push_back(std::format("also {} {}",
                                  PlainIslandSourceName(IslandSource::kChart),
                                  *island.twin));
    }
    a_offers.push_back(Offer(
        chart ? OfferGroup::kCharts : OfferGroup::kParts,
        std::format("{} {}", PlainIslandSourceName(island.source), island.id),
        Joined(facts), term));
    a_offers.back().coverage = island.share;
  }
}

void AppendMaterialOffers(std::vector<TermOffer> &a_offers,
                          const GeometryRow &a_geometry) {
  if (a_geometry.clusters.empty()) {
    a_offers.push_back(Unavailable(OfferGroup::kMaterials,
                                   std::string{kNoClusters}, ClusterTerm{}));
  }
  for (const MaterialCluster &cluster : a_geometry.clusters) {
    a_offers.push_back(
        Offer(OfferGroup::kMaterials, std::format("material {}", cluster.id),
              cluster.description, ClusterTerm{ClusterSettings{}, cluster.id}));
    a_offers.back().coverage = cluster.share;
  }
}

void AppendBoneOffers(std::vector<TermOffer> &a_offers,
                      const GeometryRow &a_geometry) {
  for (const BoneCoverage &bone : a_geometry.bones) {
    a_offers.push_back(
        Offer(OfferGroup::kBones, bone.name, {}, BoneTerm{{bone.name}}));
    a_offers.back().coverage = bone.coverage;
  }
}

void AppendPartitionOffers(std::vector<TermOffer> &a_offers,
                           const GeometryRow &a_geometry) {
  for (const SlotCoverage &partition : a_geometry.partitions) {
    a_offers.push_back(
        Offer(OfferGroup::kPartitions,
              PartitionName(a_geometry, BipedSlot{partition.slot}),
              std::format("{} triangles", partition.triangles),
              PartitionTerm{BipedSlot{partition.slot}}));
  }
}

void AppendChannelOffers(std::vector<TermOffer> &a_offers) {
  for (const MaterialChannel channel : ThresholdChannels()) {
    ThresholdTerm term;
    term.channel = channel;
    term.low = 0.5f;
    term.high = 1.0f;
    a_offers.push_back(Offer(OfferGroup::kChannels,
                             std::string{MaterialChannelName(channel)},
                             "0.5..1", term));
  }
}

void AppendPresetOffers(std::vector<TermOffer> &a_offers,
                        const MaskPresets &a_presets) {
  for (const MaskPreset &preset : a_presets.presets) {
    a_offers.push_back(Offer(OfferGroup::kPresets, preset.name,
                             preset.expression, PresetTerm{preset.name}));
  }
}

void AppendMaskOffers(std::vector<TermOffer> &a_offers,
                      const RecipeRow &a_recipe, std::string_view a_editing) {
  for (const TextRow &mask : a_recipe.maskRows) {
    if (mask.name == kScratchMask || mask.name == a_editing) {
      continue;
    }
    a_offers.push_back(Offer(OfferGroup::kMasks, mask.name, mask.text,
                             ReferenceTerm{mask.name}));
  }
}

void AppendSourceOffers(std::vector<TermOffer> &a_offers,
                        const RecipeRow &a_recipe) {
  for (const SourceRow &source : a_recipe.sourceRows) {
    const auto kind = SourceKindOf(source);
    a_offers.push_back(
        Offer(OfferGroup::kSources, source.name,
              kind ? DescribeSource(*kind)
                   : std::string{NameOf(kSourceKindWords,
                                        SourceRowKindId(source.kind))},
              ReferenceTerm{source.name}));
  }
}
}

std::vector<TermOffer> OffersOf(const MaskPresets &a_presets,
                                const RecipeRow &a_recipe,
                                const GeometryRow &a_geometry,
                                std::string_view a_editing) {
  std::vector<TermOffer> offers;
  AppendPartOffers(offers, a_geometry);
  AppendMaterialOffers(offers, a_geometry);
  AppendBoneOffers(offers, a_geometry);
  AppendPartitionOffers(offers, a_geometry);
  AppendChannelOffers(offers);
  AppendPresetOffers(offers, a_presets);
  AppendMaskOffers(offers, a_recipe, a_editing);
  AppendSourceOffers(offers, a_recipe);
  for (TermOffer &offer : offers) {
    const OfferGroupSpec *row = RowOf(kOfferGroups, offer.group);
    if (row && row->ofGeometry) {
      offer.geometry = a_geometry.name;
    }
  }
  return offers;
}

std::vector<TermOffer> OffersOfRecipe(const MaskPresets &a_presets,
                                      const RecipeRow &a_recipe,
                                      std::string_view a_editing) {
  std::vector<TermOffer> all;
  bool first = true;
  for (const GeometryRow &geometry : a_recipe.geometries) {
    for (TermOffer &offer :
         OffersOf(a_presets, a_recipe, geometry, a_editing)) {
      const OfferGroupSpec *row = RowOf(kOfferGroups, offer.group);
      if (first || (row && row->ofGeometry)) {
        all.push_back(std::move(offer));
      }
    }
    first = false;
  }
  return all;
}

std::string TermDetailOf(const Term &a_term,
                         std::span<const TermOffer> a_offers) {
  for (const TermOffer &offer : a_offers) {
    if (offer.kind == a_term.kind) {
      if (!offer.coverage) {
        return offer.detail;
      }
      const std::string share = std::format("{}%", Percent(*offer.coverage));
      return offer.detail.empty() ? share : offer.detail + ", " + share;
    }
  }
  return Match(
      a_term.kind,
      [&](const ThresholdTerm &t) {
        std::string detail =
            std::format("{} {}..{}", MaterialChannelName(t.channel),
                        ParamText(t.low), ParamText(t.high));
        if (t.posterize > 1) {
          detail += std::format(", {} levels", t.posterize);
        }
        if (t.invert) {
          detail += ", inverted";
        }
        return detail;
      },
      [&](const ClusterTerm &t) {
        return std::format("cluster {} of {}", t.id, t.settings.clusters);
      },
      [&](const IslandTerm &t) {
        return std::format("{} {}", PlainIslandSourceName(t.source), t.id);
      },
      [&](const ReferenceTerm &t) { return ReferenceText(t.name); },
      [&](const PresetTerm &t) { return t.preset; },
      [&](const PartitionTerm &t) {
        return std::format("partition {}", std::to_underlying(t.slot));
      },
      [&](const BoneTerm &t) {
        return std::to_string(t.bones.size()) + " bone(s)";
      },
      [&](const RawTerm &) { return a_term.text; });
}
}
