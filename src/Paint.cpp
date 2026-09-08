#include "Paint.h"

#include "Studio.h"

#include "Expression.h"
#include "Vocabulary.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <limits>

namespace WornEnchantmentPBR::Studio
{
	std::string PlainBoneName(const RegionsFile& a_presets, std::string_view a_bone)
	{
		const auto it = a_presets.boneNames.find(std::string{ a_bone });
		return it == a_presets.boneNames.end() ? std::string{ a_bone } : it->second;
	}

	std::string PlainPartitionName(const RegionsFile& a_presets, std::uint32_t a_slot)
	{
		const auto it = a_presets.partitionNames.find(a_slot);
		if (it != a_presets.partitionNames.end()) {
			return it->second;
		}
		const auto name = BipedSlotName(a_slot);
		return name ? std::string{ *name } : std::to_string(a_slot);
	}

	std::optional<std::string> Unresolvable(const RegionPreset& a_preset, const GeometryRow& a_geometry)
	{
		if (a_preset.kind == PresetKind::kWhat) {
			return std::nullopt;
		}
		if (!a_geometry.meshRead) {
			return "the mesh has not been read yet";
		}
		if (a_preset.partition && std::ranges::find(a_geometry.partitions, *a_preset.partition, &PartitionRow::slot) == a_geometry.partitions.end()) {
			return std::format("the geometry has no '{}' partition", BipedSlotName(*a_preset.partition).value_or("?"));
		}
		for (const auto& bone : a_preset.bones) {
			if (std::ranges::find(a_geometry.bones, bone, &BoneRow::name) == a_geometry.bones.end()) {
				return std::format("the geometry is not skinned to {}", bone);
			}
		}
		return std::nullopt;
	}

	std::optional<std::string> ScratchOf(const RecipeRow& a_recipe)
	{
		const auto it = std::ranges::find(a_recipe.maskRows, kScratchMask, &TextRow::name);
		return it != a_recipe.maskRows.end() ? std::optional{ it->text } : std::nullopt;
	}

	Existing ExistingOf(const RecipeRow& a_recipe)
	{
		Existing existing;
		for (const auto& source : a_recipe.sourceRows) {
			if (const auto kind = SourceKindOf(source)) {
				existing.sources.emplace_back(source.name, *kind);
			}
			existing.taken.push_back(source.name);
		}
		for (const auto& mask : a_recipe.masks) {
			existing.taken.push_back(mask);
		}
		return existing;
	}

	Existing ExistingOf(const Recipe& a_recipe)
	{
		Existing existing;
		for (const auto& source : a_recipe.sources) {
			existing.sources.emplace_back(source.name, source.kind);
			existing.taken.push_back(source.name);
		}
		for (const auto& mask : a_recipe.masks) {
			existing.taken.push_back(mask.name);
		}
		return existing;
	}

	std::vector<RecipeEdit> ScratchEdits(std::span<const Term> a_terms, std::optional<std::size_t> a_solo, const std::set<std::size_t>& a_muted, const std::optional<std::string>& a_scratch)
	{
		std::vector<RecipeEdit> edits;
		std::string             text = BuildRegion(a_terms, a_solo, a_muted);
		if (text.empty()) {
			text = "0";
		}
		if (!a_scratch) {
			edits.push_back(AddMask{ std::string{ kScratchMask } });
		}
		if (!a_scratch || *a_scratch != text) {
			edits.push_back(SetMask{ std::string{ kScratchMask }, std::move(text) });
		}
		return edits;
	}

	namespace
	{
		class SourceNamer
		{
		public:
			explicit SourceNamer(const Existing& a_existing) :
				existing_(a_existing), taken_(a_existing.taken)
			{}

			[[nodiscard]] std::string NameFor(const std::string& a_wanted, const SourceKind& a_kind)
			{
				for (const auto& [name, kind] : existing_.sources) {
					if (name == a_wanted && kind == a_kind) {
						return name;
					}
				}
				for (const auto& [name, kind] : existing_.sources) {
					if (kind == a_kind) {
						return name;
					}
				}
				const auto name = UniqueName(a_wanted, taken_);
				taken_.push_back(name);
				edits_.push_back(AddSource{ name, a_kind });
				return name;
			}

			[[nodiscard]] std::vector<RecipeEdit> Edits() && { return std::move(edits_); }

		private:
			const Existing&          existing_;
			std::vector<std::string> taken_;
			std::vector<RecipeEdit>  edits_;
		};
	}

	BuiltTerm MaterialiseTerm(const RegionPreset& a_preset, const Existing& a_existing)
	{
		SourceNamer namer(a_existing);
		std::string expression;
		if (a_preset.kind == PresetKind::kWhere) {
			std::vector<std::string> terms;
			if (a_preset.partition) {
				terms.push_back(ReferenceText(namer.NameFor("partition", BakeSource{ PartitionBake{ *a_preset.partition } })));
			}
			if (!a_preset.bones.empty()) {
				terms.push_back(ReferenceText(namer.NameFor("bones", BakeSource{ BoneWeightBake{ a_preset.bones } })));
			}
			for (const auto& term : terms) {
				expression += (expression.empty() ? "" : " * ") + term;
			}
		} else {
			expression = a_preset.expression;
			for (const auto& [wanted, kind] : a_preset.sources) {
				const auto name = namer.NameFor(wanted, kind);
				if (name != wanted) {
					expression = RenameInExpression(expression, wanted, name, false);
				}
			}
		}
		return BuiltTerm{ std::move(namer).Edits(), std::move(expression) };
	}

	std::string TermLabel(std::string_view a_text, const RegionsFile& a_presets, const Existing& a_existing)
	{
		if (a_text.size() > 1 && a_text.front() == '@' && IsName(a_text.substr(1))) {
			return std::string{ a_text.substr(1) };
		}
		for (const auto* list : { &a_presets.where, &a_presets.what }) {
			for (const auto& preset : *list) {
				const auto term = MaterialiseTerm(preset, a_existing);
				if (term.edits.empty() && term.expression == a_text) {
					return preset.name;
				}
			}
		}
		return std::string{ kExpressionLabel };
	}

	std::string ProposedRegionName(std::span<const Term> a_terms, std::string_view a_editing)
	{
		if (!a_editing.empty()) {
			return std::string{ a_editing };
		}
		std::string name;
		for (const auto& term : a_terms) {
			if (term.label.empty() || term.label == kExpressionLabel || term.text.empty()) {
				continue;
			}
			std::string part = term.label.front() == '@' ? term.label.substr(1) : term.label;
			if (part.empty()) {
				continue;
			}
			if (!name.empty()) {
				part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
			}
			name += part;
		}
		return IsName(name) ? name : "region";
	}

	std::string_view TermKindName(const TermKind& a_kind) noexcept
	{
		return Match(
			a_kind,
			[](const RawTerm&) { return "raw"; }, [](const ReferenceTerm&) { return "reference"; }, [](const ThresholdTerm&) { return "threshold"; },
			[](const WhatPresetTerm&) { return "preset"; }, [](const PartitionTerm&) { return "partition"; }, [](const BoneTerm&) { return "bones"; },
			[](const IslandTerm&) { return "component"; }, [](const ClusterTerm&) { return "cluster"; });
	}

	namespace
	{
		[[nodiscard]] std::vector<MaterialChannel> ThresholdChannels()
		{
			std::vector<MaterialChannel> channels;
			for (const auto& row : kMaterialChannels) {
				if (Thresholdable(row.value)) {
					channels.push_back(row.value);
				}
			}
			return channels;
		}

		[[nodiscard]] std::vector<std::string> ThresholdChannelNames()
		{
			std::vector<std::string> names;
			for (const auto channel : ThresholdChannels()) {
				names.emplace_back(MaterialChannelName(channel));
			}
			return names;
		}

		[[nodiscard]] std::string NumberText(float a_value)
		{
			return ParamText(Param{ a_value });
		}

		[[nodiscard]] std::optional<float> ReadNumber(std::string_view a_text)
		{
			while (!a_text.empty() && a_text.front() == ' ') {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && a_text.back() == ' ') {
				a_text.remove_suffix(1);
			}
			float      value = 0.0f;
			const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() || !std::isfinite(value)) {
				return std::nullopt;
			}
			return value;
		}

		[[nodiscard]] std::optional<std::uint32_t> ReadWhole(std::string_view a_text, std::uint32_t a_max)
		{
			while (!a_text.empty() && a_text.front() == ' ') {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && a_text.back() == ' ') {
				a_text.remove_suffix(1);
			}
			std::uint32_t value = 0;
			const auto    result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size() || value > a_max) {
				return std::nullopt;
			}
			return value;
		}

		[[nodiscard]] float Spelled(float a_value)
		{
			return ReadNumber(NumberText(a_value)).value_or(a_value);
		}

		[[nodiscard]] int Percent(float a_share)
		{
			return static_cast<int>(std::lround(std::clamp(a_share, 0.0f, 1.0f) * 100.0f));
		}

		[[nodiscard]] const MeshIsland* RegionOf(const GeometryRow& a_geometry, IslandSource a_source, std::uint16_t a_id)
		{
			for (const auto& island : a_geometry.islands) {
				if (island.source == a_source && island.id == a_id) {
					return &island;
				}
			}
			return nullptr;
		}

		[[nodiscard]] const MaterialCluster* ClusterOf(const GeometryRow& a_geometry, std::uint8_t a_id)
		{
			for (const auto& cluster : a_geometry.clusters) {
				if (cluster.id == a_id) {
					return &cluster;
				}
			}
			return nullptr;
		}

		[[nodiscard]] std::string OperandText(const std::string& a_name, std::uint8_t a_posterize)
		{
			const auto reference = ReferenceText(a_name);
			if (a_posterize > 1) {
				return std::format("floor({} * {}) / {}", reference, a_posterize, a_posterize);
			}
			return reference;
		}

		[[nodiscard]] std::string EdgeText(float a_centre, float a_softness, const std::string& a_operand)
		{
			const auto centre = NumberText(a_centre);
			const auto softness = NumberText(a_softness);
			return std::format("smoothstep({} - {}, {} + {}, {})", centre, softness, centre, softness, a_operand);
		}

		[[nodiscard]] std::string ThresholdText(const ThresholdTerm& a_term, const std::string& a_name)
		{
			const auto  operand = OperandText(a_name, a_term.posterize);
			const bool  hasLow = a_term.low != 0.0f;
			const bool  hasHigh = a_term.high != 1.0f;
			std::string body;
			if (hasLow && hasHigh) {
				body = std::format("{} * (1 - {})", EdgeText(a_term.low, a_term.softness, operand), EdgeText(a_term.high, a_term.softness, operand));
			} else if (hasLow) {
				body = EdgeText(a_term.low, a_term.softness, operand);
			} else if (hasHigh) {
				body = std::format("1 - {}", EdgeText(a_term.high, a_term.softness, operand));
			} else {
				body = std::format("step(0, {})", operand);
			}
			return a_term.invert ? std::format("1 - ({})", body) : body;
		}

		[[nodiscard]] std::string RegionText(const std::string& a_name, std::uint32_t a_id)
		{
			return std::format("abs({} * 255 - {}) < 0.5", ReferenceText(a_name), a_id);
		}

		[[nodiscard]] const RegionPreset* WhatPresetNamed(const RegionsFile& a_presets, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_presets.what, a_name, &RegionPreset::name);
			return it == a_presets.what.end() ? nullptr : &*it;
		}
	}

	BuiltTerm BuildTerm(const TermKind& a_kind, const RegionsFile& a_presets, const Existing& a_existing)
	{
		SourceNamer namer(a_existing);
		std::string text = Match(
			a_kind,
			[](const RawTerm&) { return std::string{}; },
			[](const ReferenceTerm& t) { return ReferenceText(t.name); },
			[&](const ThresholdTerm& t) {
				const auto name = namer.NameFor(std::string{ MaterialChannelName(t.channel) }, MaterialSource{ t.channel });
				return ThresholdText(t, name);
			},
			[&](const WhatPresetTerm& t) {
				const auto* preset = WhatPresetNamed(a_presets, t.preset);
				if (!preset) {
					return std::string{ "0" };
				}
				auto term = MaterialiseTerm(*preset, a_existing);
				for (auto& edit : term.edits) {
					if (const auto* add = Get<AddSource>(edit)) {
						(void)namer.NameFor(add->name, add->kind);
					}
				}
				return term.expression;
			},
			[&](const PartitionTerm& t) { return ReferenceText(namer.NameFor("partition", BakeSource{ PartitionBake{ t.slot } })); },
			[&](const BoneTerm& t) { return ReferenceText(namer.NameFor("bones", BakeSource{ BoneWeightBake{ t.bones } })); },
			[&](const IslandTerm& t) {
				const auto name = namer.NameFor(t.source == IslandSource::kChart ? "charts" : "components", IslandBakeOf(t.source));
				return RegionText(name, t.id);
			},
			[&](const ClusterTerm& t) {
				const auto name = namer.NameFor("clusters", SourceOf(t.settings));
				return RegionText(name, t.id);
			});
		return BuiltTerm{ std::move(namer).Edits(), std::move(text) };
	}

	std::string TermLabelOf(const TermKind& a_kind, const RegionsFile& a_presets, const GeometryRow& a_geometry)
	{
		return Match(
			a_kind,
			[](const RawTerm&) { return std::string{ kExpressionLabel }; },
			[](const ReferenceTerm& t) { return ReferenceText(t.name); },
			[](const ThresholdTerm& t) { return std::format("{} {}..{}", MaterialChannelName(t.channel), NumberText(t.low), NumberText(t.high)); },
			[](const WhatPresetTerm& t) { return t.preset; },
			[&](const PartitionTerm& t) { return PlainPartitionName(a_presets, t.slot); },
			[&](const BoneTerm& t) {
				std::string label;
				for (const auto& bone : t.bones) {
					label += (label.empty() ? "" : ", ") + PlainBoneName(a_presets, bone);
				}
				return label;
			},
			[&](const IslandTerm& t) {
				const auto* region = RegionOf(a_geometry, t.source, t.id);
				if (!region) {
					return std::format("{} {}", PlainIslandSourceName(t.source), t.id);
				}
				if (region->dominantBone.empty()) {
					return std::format("{} {}: {}%", PlainIslandSourceName(t.source), t.id, Percent(region->share));
				}
				return std::format("{} {}: {}, {}%", PlainIslandSourceName(t.source), t.id, PlainBoneName(a_presets, region->dominantBone), Percent(region->share));
			},
			[&](const ClusterTerm& t) {
				const auto* cluster = ClusterOf(a_geometry, t.id);
				if (!cluster) {
					return std::format("material {}", t.id);
				}
				return std::format("material {}: {}, {}%", t.id, cluster->description, Percent(cluster->share));
			});
	}

	namespace
	{
		[[nodiscard]] FormField Spec(std::string a_name, FieldKind a_kind, std::string a_text, std::vector<std::string> a_names = {})
		{
			FormField field;
			field.name = std::move(a_name);
			field.kind = a_kind;
			field.text = std::move(a_text);
			field.names = std::move(a_names);
			return field;
		}

		template <class Recipe>
		[[nodiscard]] TermField Setting(const Recipe& a_kind, FormField a_field, std::function<bool(Recipe&, const std::string&)> a_set)
		{
			return TermField{ std::move(a_field), [a_kind, a_set](const std::string& a_text) -> std::optional<TermKind> {
				Recipe edited = a_kind;
				return a_set(edited, a_text) ? std::optional<TermKind>{ edited } : std::nullopt;
			} };
		}

		[[nodiscard]] std::optional<float> NumberIn(const std::string& a_text, float a_min, float a_max)
		{
			const auto number = ReadNumber(a_text);
			if (!number || *number < a_min || *number > a_max) {
				return std::nullopt;
			}
			return Spelled(*number);
		}

		[[nodiscard]] std::string OnOff(bool a_on)
		{
			return a_on ? "on" : "off";
		}

		[[nodiscard]] std::string BoneListText(const std::vector<std::string>& a_bones)
		{
			std::string text;
			for (const auto& bone : a_bones) {
				text += (text.empty() ? "" : ", ") + bone;
			}
			return text;
		}

		[[nodiscard]] std::optional<std::vector<std::string>> ParseBoneList(std::string_view a_text)
		{
			std::vector<std::string> bones;
			while (!a_text.empty()) {
				const auto comma = a_text.find(',');
				auto       part = a_text.substr(0, comma);
				a_text = comma == std::string_view::npos ? std::string_view{} : a_text.substr(comma + 1);
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

		std::vector<TermField> ThresholdForm(const ThresholdTerm& a_term)
		{
			using Set = std::function<bool(ThresholdTerm&, const std::string&)>;
			std::vector<TermField> form;
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("channel", FieldKind::kChoice, std::string{ MaterialChannelName(a_term.channel) }, ThresholdChannelNames()), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				const auto channel = ParseMaterialChannel(a_text);
				if (!channel || !Thresholdable(*channel)) {
					return false;
				}
				t.channel = *channel;
				return true;
			} }));
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("low", FieldKind::kScalar, NumberText(a_term.low)), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				const auto number = NumberIn(a_text, -1.0f, 2.0f);
				return number ? (t.low = *number, true) : false;
			} }));
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("high", FieldKind::kScalar, NumberText(a_term.high)), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				const auto number = NumberIn(a_text, -1.0f, 2.0f);
				return number ? (t.high = *number, true) : false;
			} }));
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("softness", FieldKind::kScalar, NumberText(a_term.softness)), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				const auto number = NumberIn(a_text, 0.0f, 1.0f);
				return number ? (t.softness = *number, true) : false;
			} }));
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("posterize", FieldKind::kScalar, std::to_string(a_term.posterize)), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				const auto levels = ReadWhole(a_text, 255);
				return levels ? (t.posterize = static_cast<std::uint8_t>(*levels), true) : false;
			} }));
			form.push_back(Setting<ThresholdTerm>(a_term, Spec("invert", FieldKind::kToggle, OnOff(a_term.invert)), Set{ [](ThresholdTerm& t, const std::string& a_text) {
				if (a_text != "on" && a_text != "off") {
					return false;
				}
				t.invert = a_text == "on";
				return true;
			} }));
			return form;
		}

		std::vector<TermField> ClusterForm(const ClusterTerm& a_term)
		{
			using Set = std::function<bool(ClusterTerm&, const std::string&)>;
			std::vector<TermField> form;
			form.push_back(Setting<ClusterTerm>(a_term, Spec("clusters", FieldKind::kScalar, std::to_string(a_term.settings.clusters)), Set{ [](ClusterTerm& t, const std::string& a_text) {
				const auto count = ReadWhole(a_text, kMaxClusters);
				if (!count || *count < 1) {
					return false;
				}
				t.settings.clusters = static_cast<std::uint8_t>(*count);
				return true;
			} }));
			const auto weight = [&](const char* a_name, float ChannelWeights::*a_member) {
				form.push_back(Setting<ClusterTerm>(a_term, Spec(a_name, FieldKind::kScalar, NumberText(a_term.settings.weights.*a_member)), Set{ [a_member](ClusterTerm& t, const std::string& a_text) {
					const auto number = NumberIn(a_text, 0.0f, kMaxChannelWeight);
					return number ? (t.settings.weights.*a_member = *number, true) : false;
				} }));
			};
			weight("roughness", &ChannelWeights::roughness);
			weight("metallic", &ChannelWeights::metallic);
			weight("occlusion", &ChannelWeights::occlusion);
			weight("reflectance", &ChannelWeights::reflectance);
			weight("luma", &ChannelWeights::luma);
			form.push_back(Setting<ClusterTerm>(a_term, Spec("seed", FieldKind::kScalar, std::to_string(a_term.settings.seed)), Set{ [](ClusterTerm& t, const std::string& a_text) {
				const auto seed = ReadWhole(a_text, std::numeric_limits<std::uint32_t>::max());
				return seed ? (t.settings.seed = *seed, true) : false;
			} }));
			return form;
		}

		std::vector<TermField> ComponentForm(const IslandTerm& a_term, const RegionsFile& a_presets, const GeometryRow& a_geometry)
		{
			std::vector<std::string>   labels;
			std::vector<std::uint16_t> ids;
			for (const auto& island : a_geometry.islands) {
				if (island.source == a_term.source) {
					labels.push_back(TermLabelOf(IslandTerm{ a_term.source, island.id }, a_presets, a_geometry));
					ids.push_back(island.id);
				}
			}
			std::vector<TermField> form;
			form.push_back(Setting<IslandTerm>(a_term, Spec("id", FieldKind::kChoice, TermLabelOf(a_term, a_presets, a_geometry), labels), [labels, ids](IslandTerm& t, const std::string& a_text) {
				const auto it = std::ranges::find(labels, a_text);
				if (it != labels.end()) {
					const auto index = static_cast<std::size_t>(it - labels.begin());
					t.id = index < ids.size() ? ids[index] : t.id;
					return index < ids.size();
				}
				const auto id = ReadWhole(a_text, kMaxIslands);
				return id ? (t.id = static_cast<std::uint16_t>(*id), true) : false;
			}));
			return form;
		}

		std::vector<TermField> BoneForm(const BoneTerm& a_term)
		{
			std::vector<TermField> form;
			form.push_back(Setting<BoneTerm>(a_term, Spec("bones", FieldKind::kText, BoneListText(a_term.bones)), [](BoneTerm& t, const std::string& a_text) {
				const auto bones = ParseBoneList(a_text);
				return bones ? (t.bones = *bones, true) : false;
			}));
			return form;
		}

		std::vector<TermField> PartitionForm(const PartitionTerm& a_term, const RegionsFile& a_presets, const GeometryRow& a_geometry)
		{
			std::vector<std::string>   names;
			std::vector<std::uint32_t> slots;
			for (const auto& partition : a_geometry.partitions) {
				names.push_back(PlainPartitionName(a_presets, partition.slot));
				slots.push_back(partition.slot);
			}
			std::vector<TermField> form;
			form.push_back(Setting<PartitionTerm>(a_term, Spec("slot", FieldKind::kChoice, PlainPartitionName(a_presets, a_term.slot), names), [names, slots](PartitionTerm& t, const std::string& a_text) {
				const auto it = std::ranges::find(names, a_text);
				if (it != names.end()) {
					const auto index = static_cast<std::size_t>(it - names.begin());
					t.slot = index < slots.size() ? slots[index] : t.slot;
					return index < slots.size();
				}
				const auto named = BipedSlotFromName(a_text);
				const auto numbered = ReadWhole(a_text, 61);
				const auto slot = named ? named : (numbered && *numbered >= 30 ? numbered : std::nullopt);
				return slot ? (t.slot = *slot, true) : false;
			}));
			return form;
		}
	}

	std::vector<TermField> TermForm(const TermKind& a_kind, const RegionsFile& a_presets, const GeometryRow& a_geometry)
	{
		return Match(
			a_kind,
			[](const RawTerm&) { return std::vector<TermField>{}; },
			[](const ReferenceTerm&) { return std::vector<TermField>{}; },
			[](const WhatPresetTerm&) { return std::vector<TermField>{}; },
			[](const ThresholdTerm& t) { return ThresholdForm(t); },
			[](const ClusterTerm& t) { return ClusterForm(t); },
			[&](const IslandTerm& t) { return ComponentForm(t, a_presets, a_geometry); },
			[](const BoneTerm& t) { return BoneForm(t); },
			[&](const PartitionTerm& t) { return PartitionForm(t, a_presets, a_geometry); });
	}

	namespace
	{
		[[nodiscard]] TermOffer Offer(OfferGroup a_group, std::string a_name, std::string a_detail, TermKind a_kind)
		{
			TermOffer offer;
			offer.group = a_group;
			offer.name = std::move(a_name);
			offer.detail = std::move(a_detail);
			offer.kind = std::move(a_kind);
			return offer;
		}

		[[nodiscard]] std::string Joined(std::span<const std::string> a_facts)
		{
			std::string text;
			for (const auto& fact : a_facts) {
				text += text.empty() ? fact : ", " + fact;
			}
			return text;
		}

		[[nodiscard]] TermOffer Unavailable(OfferGroup a_group, std::string a_reason, TermKind a_kind)
		{
			TermOffer offer = Offer(a_group, std::string{ NameOf(kOfferGroups, a_group) }, {}, std::move(a_kind));
			offer.unavailable = std::move(a_reason);
			return offer;
		}

		constexpr std::string_view kMeshUnread = "the mesh has not been read yet";
		constexpr std::string_view kNoClusters = "the material has no clusters yet";
	}

	std::vector<TermOffer> OffersOf(const RegionsFile& a_presets, const RecipeRow& a_recipe, const GeometryRow& a_geometry, std::string_view a_editing)
	{
		std::vector<TermOffer> offers;
		if (!a_geometry.meshRead) {
			offers.push_back(Unavailable(OfferGroup::kParts, std::string{ kMeshUnread }, IslandTerm{}));
		}
		for (const auto& island : a_geometry.islands) {
			const bool chart = island.source == IslandSource::kChart;
			if (chart && island.twin) {
				continue;
			}
			const IslandTerm         term{ island.source, island.id };
			std::vector<std::string> facts;
			if (!island.dominantBone.empty()) {
				facts.push_back(std::format("{} {}%", PlainBoneName(a_presets, island.dominantBone), Percent(island.dominantShare)));
			}
			if (island.twin) {
				facts.push_back(std::format("also {} {}", PlainIslandSourceName(IslandSource::kChart), *island.twin));
			}
			offers.push_back(Offer(chart ? OfferGroup::kCharts : OfferGroup::kParts, std::format("{} {}", PlainIslandSourceName(island.source), island.id), Joined(facts), term));
			offers.back().coverage = island.share;
		}
		if (a_geometry.clusters.empty()) {
			offers.push_back(Unavailable(OfferGroup::kMaterials, std::string{ kNoClusters }, ClusterTerm{}));
		}
		for (const auto& cluster : a_geometry.clusters) {
			offers.push_back(Offer(OfferGroup::kMaterials, std::format("material {}", cluster.id), cluster.description, ClusterTerm{ ClusterSettings{}, cluster.id }));
			offers.back().coverage = cluster.share;
		}
		for (const auto& bone : a_geometry.bones) {
			offers.push_back(Offer(OfferGroup::kBones, PlainBoneName(a_presets, bone.name), {}, BoneTerm{ { bone.name } }));
			offers.back().coverage = bone.coverage;
		}
		for (const auto& partition : a_geometry.partitions) {
			offers.push_back(Offer(OfferGroup::kPartitions, PlainPartitionName(a_presets, partition.slot), std::format("{} triangles", partition.triangles), PartitionTerm{ partition.slot }));
		}
		for (const auto channel : ThresholdChannels()) {
			ThresholdTerm term;
			term.channel = channel;
			term.low = 0.5f;
			term.high = 1.0f;
			offers.push_back(Offer(OfferGroup::kChannels, std::string{ MaterialChannelName(channel) }, "0.5..1", term));
		}
		for (const auto& preset : a_presets.what) {
			offers.push_back(Offer(OfferGroup::kPresets, preset.name, preset.expression, WhatPresetTerm{ preset.name }));
		}
		for (const auto& mask : a_recipe.maskRows) {
			if (mask.name == kScratchMask || mask.name == a_editing) {
				continue;
			}
			offers.push_back(Offer(OfferGroup::kMasks, mask.name, mask.text, ReferenceTerm{ mask.name }));
		}
		for (const auto& source : a_recipe.sourceRows) {
			const auto kind = SourceKindOf(source);
			offers.push_back(Offer(OfferGroup::kSources, source.name, kind ? DescribeSource(*kind) : source.kind, ReferenceTerm{ source.name }));
		}
		for (auto& offer : offers) {
			const auto* row = RowOf(kOfferGroups, offer.group);
			if (row && row->ofGeometry) {
				offer.geometry = a_geometry.name;
			}
		}
		return offers;
	}

	std::vector<TermOffer> OffersOfRecipe(const RegionsFile& a_presets, const RecipeRow& a_recipe, std::string_view a_editing)
	{
		std::vector<TermOffer> all;
		bool                   first = true;
		for (const auto& geometry : a_recipe.geometries) {
			for (auto& offer : OffersOf(a_presets, a_recipe, geometry, a_editing)) {
				const auto* row = RowOf(kOfferGroups, offer.group);
				if (first || (row && row->ofGeometry)) {
					all.push_back(std::move(offer));
				}
			}
			first = false;
		}
		return all;
	}

	std::string TermDetailOf(const Term& a_term, std::span<const TermOffer> a_offers)
	{
		for (const auto& offer : a_offers) {
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
			[&](const ThresholdTerm& t) {
				std::string detail = std::format("{} {}..{}", MaterialChannelName(t.channel), ParamText(t.low), ParamText(t.high));
				if (t.posterize > 1) {
					detail += std::format(", {} levels", t.posterize);
				}
				if (t.invert) {
					detail += ", inverted";
				}
				return detail;
			},
			[&](const ClusterTerm& t) { return std::format("cluster {} of {}", t.id, t.settings.clusters); },
			[&](const IslandTerm& t) { return std::format("{} {}", PlainIslandSourceName(t.source), t.id); },
			[&](const ReferenceTerm& t) { return ReferenceText(t.name); },
			[&](const WhatPresetTerm& t) { return t.preset; },
			[&](const PartitionTerm& t) { return std::format("partition {}", t.slot); },
			[&](const BoneTerm& t) { return std::to_string(t.bones.size()) + " bone(s)"; },
			[&](const RawTerm&) { return a_term.text; });
	}

	SurfaceOutput PaintOutput(Surface a_surface)
	{
		SurfaceOutput output = DefaultOutput(a_surface, Slot::kEmissive);
		Layer          layer = DefaultLayer();
		layer.mask = Ref{ std::string{ kScratchMask } };
		output.stack = { std::move(layer) };
		return output;
	}

	std::vector<RecipeEdit> PaintSurfaceEdits(Surface a_surface)
	{
		Layer layer = DefaultLayer();
		layer.mask = Ref{ std::string{ kScratchMask } };
		return { RemoveOutput{ 0 }, AddOutput{ a_surface, Slot::kEmissive, Selector{} }, AddLayer{ 0, std::move(layer), std::nullopt } };
	}

	Recipe PaintRecipe(const Recipe& a_active, RecipeKey a_key, Surface a_surface)
	{
		Recipe recipe = a_active;
		recipe.id = std::string{ kPaintRecipe };
		recipe.metadata = Metadata{};
		recipe.metadata.name = recipe.id;
		recipe.keys = { std::move(a_key) };
		recipe.priority = kPaintPriority;
		recipe.variants.clear();
		recipe.outputs = { PaintOutput(a_surface) };
		std::erase_if(recipe.masks, [](const Mask& m) { return m.name == kScratchMask; });
		recipe.masks.push_back(Mask{ std::string{ kScratchMask }, "0" });
		return recipe;
	}

	std::vector<RecipeEdit> KeepEdits(const Recipe& a_paint, const Recipe& a_active, std::string_view a_name)
	{
		std::vector<RecipeEdit> edits;
		const auto*             scratch = a_paint.FindMask(kScratchMask);
		if (!scratch) {
			return edits;
		}
		std::string text = scratch->text;
		const auto  program = Program::Parse(text);
		if (!program) {
			return edits;
		}
		const Existing existing = ExistingOf(a_active);
		SourceNamer    namer(existing);
		for (const auto& read : program->References()) {
			const auto* source = a_paint.FindSource(read);
			if (!source) {
				continue;
			}
			const std::string to = namer.NameFor(read, source->kind);
			if (to != read) {
				text = RenameInExpression(text, read, to, false);
			}
		}
		edits = std::move(namer).Edits();
		if (!a_active.FindMask(a_name)) {
			edits.push_back(AddMask{ std::string{ a_name } });
		}
		edits.push_back(SetMask{ std::string{ a_name }, std::move(text) });
		return edits;
	}
}
