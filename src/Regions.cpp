#include "Regions.h"

#include "Studio.h"

#include <nlohmann/json.hpp>

#include "Expression.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <limits>

namespace WornEnchantmentPBR::Studio
{
	using json = nlohmann::json;

	std::vector<PartitionRow> PartitionsOf(const MeshData& a_mesh)
	{
		std::vector<PartitionRow> rows;
		for (const auto& partition : a_mesh.partitions) {
			if (partition.slot == MeshPartition::kNoSlot) {
				continue;
			}
			const auto it = std::ranges::find(rows, partition.slot, &PartitionRow::slot);
			if (it != rows.end()) {
				it->triangles += partition.triangles.size();
				continue;
			}
			PartitionRow row;
			row.slot = partition.slot;
			const auto name = BipedSlotName(partition.slot);
			row.name = name ? std::string{ *name } : std::to_string(partition.slot);
			row.triangles = partition.triangles.size();
			rows.push_back(std::move(row));
		}
		return rows;
	}

	std::vector<BoneRow> BonesOf(const MeshData& a_mesh)
	{
		// Coverage: the summed weight a bone carries over every vertex, as a
		// share of all vertices; a bone moving half the mesh fully reads 0.5.
		std::map<std::string, float> weight;
		std::size_t                  vertices = 0;
		for (const auto& partition : a_mesh.partitions) {
			for (const auto& vertex : partition.vertices) {
				++vertices;
				for (std::size_t i = 0; i < 4; ++i) {
					if (vertex.weights[i] <= 0.0f || vertex.bones[i] >= partition.boneNames.size()) {
						continue;
					}
					weight[partition.boneNames[vertex.bones[i]]] += vertex.weights[i];
				}
			}
		}
		std::vector<BoneRow> rows;
		for (const auto& [name, sum] : weight) {
			rows.push_back(BoneRow{ name, vertices > 0 ? sum / static_cast<float>(vertices) : 0.0f });
		}
		std::ranges::sort(rows, [](const BoneRow& a, const BoneRow& b) { return a.coverage > b.coverage; });
		return rows;
	}

	namespace
	{
		[[nodiscard]] std::optional<SourceKind> SourceFromJson(const json& a_value)
		{
			if (!a_value.is_object() || a_value.size() != 1) {
				return std::nullopt;
			}
			const auto& [key, value] = *a_value.items().begin();
			if (key == "material" && value.is_string()) {
				const auto channel = ParseMaterialChannel(value.get<std::string>());
				return channel ? std::optional<SourceKind>{ MaterialSource{ *channel } } : std::nullopt;
			}
			if (key == "bake" && value.is_string()) {
				const auto bake = DefaultBakeKind(value.get<std::string>());
				return bake ? std::optional<SourceKind>{ BakeSource{ *bake } } : std::nullopt;
			}
			if (key == "uv" && value.is_string()) {
				const auto axis = ParseUvAxis(value.get<std::string>());
				return axis ? std::optional<SourceKind>{ UvSource{ *axis } } : std::nullopt;
			}
			return std::nullopt;
		}
	}

	std::expected<Presets, std::string> ParsePresets(std::string_view a_json)
	{
		const auto parsed = json::parse(a_json, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return std::unexpected("the preset file is not a JSON object");
		}
		Presets presets;
		if (const auto names = parsed.find("names"); names != parsed.end() && names->is_object()) {
			if (const auto partitions = names->find("partitions"); partitions != names->end() && partitions->is_object()) {
				for (const auto& [slot, name] : partitions->items()) {
					const auto number = std::strtoul(slot.c_str(), nullptr, 10);
					if (number >= 30 && number <= 61 && name.is_string()) {
						presets.partitionNames[static_cast<std::uint32_t>(number)] = name.get<std::string>();
					}
				}
			}
			if (const auto bones = names->find("bones"); bones != names->end() && bones->is_object()) {
				for (const auto& [bone, name] : bones->items()) {
					if (name.is_string()) {
						presets.boneNames[bone] = name.get<std::string>();
					}
				}
			}
		}
		if (const auto where = parsed.find("where"); where != parsed.end() && where->is_array()) {
			if (where->size() > kMaxPresets) {
				return std::unexpected(std::format("more than {} where presets", kMaxPresets));
			}
			for (const auto& entry : *where) {
				RegionPreset preset;
				preset.kind = PresetKind::kWhere;
				if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() || !IsName(entry["name"].get<std::string>())) {
					return std::unexpected("a where preset needs a name");
				}
				preset.name = entry["name"].get<std::string>();
				if (entry.contains("partition")) {
					const auto& partition = entry["partition"];
					const auto slot = partition.is_string() ? BipedSlotFromName(partition.get<std::string>()) : (partition.is_number_unsigned() ? std::optional{ partition.get<std::uint32_t>() } : std::nullopt);
					if (!slot) {
						return std::unexpected(std::format("where preset {}: unknown partition", preset.name));
					}
					preset.partition = *slot;
				}
				if (const auto bones = entry.find("bones"); bones != entry.end() && bones->is_array()) {
					if (bones->size() > kMaxPresetBones) {
						return std::unexpected(std::format("where preset {}: more than {} bones", preset.name, kMaxPresetBones));
					}
					for (const auto& bone : *bones) {
						if (bone.is_string()) {
							preset.bones.push_back(bone.get<std::string>());
						}
					}
				}
				if (!preset.partition && preset.bones.empty()) {
					return std::unexpected(std::format("where preset {}: needs a partition or bones", preset.name));
				}
				presets.where.push_back(std::move(preset));
			}
		}
		if (const auto what = parsed.find("what"); what != parsed.end() && what->is_array()) {
			if (what->size() > kMaxPresets) {
				return std::unexpected(std::format("more than {} what presets", kMaxPresets));
			}
			for (const auto& entry : *what) {
				RegionPreset preset;
				preset.kind = PresetKind::kWhat;
				if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string() || !IsName(entry["name"].get<std::string>())) {
					return std::unexpected("a what preset needs a name");
				}
				preset.name = entry["name"].get<std::string>();
				if (!entry.contains("expression") || !entry["expression"].is_string()) {
					return std::unexpected(std::format("what preset {}: needs an expression", preset.name));
				}
				preset.expression = entry["expression"].get<std::string>();
				if (preset.expression.size() > kMaxExpressionLength) {
					return std::unexpected(std::format("what preset {}: expression longer than {} characters", preset.name, kMaxExpressionLength));
				}
				if (const auto program = Program::Parse(preset.expression); !program) {
					return std::unexpected(std::format("what preset {}: expression: {}", preset.name, program.error()));
				}
				if (const auto sources = entry.find("sources"); sources != entry.end() && sources->is_object()) {
					if (sources->size() > kMaxPresetSources) {
						return std::unexpected(std::format("what preset {}: more than {} sources", preset.name, kMaxPresetSources));
					}
					for (const auto& [name, definition] : sources->items()) {
						const auto kind = SourceFromJson(definition);
						if (!IsName(name) || !kind) {
							return std::unexpected(std::format("what preset {}: source '{}' is not a material channel, bake or uv", preset.name, name));
						}
						preset.sources.emplace_back(name, *kind);
					}
				}
				presets.what.push_back(std::move(preset));
			}
		}
		return presets;
	}

	std::string PlainBoneName(const Presets& a_presets, std::string_view a_bone)
	{
		const auto it = a_presets.boneNames.find(std::string{ a_bone });
		return it == a_presets.boneNames.end() ? std::string{ a_bone } : it->second;
	}

	std::string PlainPartitionName(const Presets& a_presets, std::uint32_t a_slot)
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
			return std::format("the shape has no '{}' partition", BipedSlotName(*a_preset.partition).value_or("?"));
		}
		for (const auto& bone : a_preset.bones) {
			if (std::ranges::find(a_geometry.bones, bone, &BoneRow::name) == a_geometry.bones.end()) {
				return std::format("the shape is not skinned to {}", bone);
			}
		}
		return std::nullopt;
	}

	ScratchState ScratchOf(const RecipeRow& a_recipe)
	{
		ScratchState scratch;
		const auto   it = std::ranges::find(a_recipe.maskRows, kScratchMask, &TextRow::name);
		if (it != a_recipe.maskRows.end()) {
			scratch.present = true;
			scratch.text = it->text;
		}
		return scratch;
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

	std::vector<RecipeEdit> ScratchEdits(std::span<const Term> a_terms, std::optional<std::size_t> a_solo, const std::set<std::size_t>& a_muted, const ScratchState& a_scratch)
	{
		std::vector<RecipeEdit> edits;
		std::string             text = BuildRegion(a_terms, a_solo, a_muted);
		if (text.empty()) {
			text = "0";
		}
		if (!a_scratch.present) {
			edits.push_back(AddMask{ std::string{ kScratchMask } });
		}
		if (!a_scratch.present || a_scratch.text != text) {
			edits.push_back(SetMask{ std::string{ kScratchMask }, std::move(text) });
		}
		return edits;
	}

	namespace
	{
		// The name an expression reads a definition by: an existing twin's
		// name, else a new row named as wanted, made unique among the taken
		// names. Both the presets and the templates name their sources by
		// this rule, so a term never adds a row the recipe already has.
		class SourceNamer
		{
		public:
			explicit SourceNamer(const Existing& a_existing) :
				existing_(a_existing), taken_(a_existing.taken)
			{}

			[[nodiscard]] std::string NameFor(const std::string& a_wanted, const SourceKind& a_kind)
			{
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

	PresetTerm MaterialiseTerm(const RegionPreset& a_preset, const Existing& a_existing)
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
		return PresetTerm{ std::move(namer).Edits(), std::move(expression) };
	}

	std::string TermLabel(std::string_view a_text, const Presets& a_presets, const Existing& a_existing)
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

	namespace
	{
		// Defined with the templates below; TermsOfMask reads each term through it.
		TermRecipe ReadTermOver(std::string_view a_text, const Presets& a_presets, const Existing& a_existing);
	}

	std::optional<std::vector<Term>> TermsOfMask(std::string_view a_text, const Presets& a_presets, const Existing& a_existing)
	{
		auto terms = ParseRegion(a_text);
		if (!terms) {
			return std::nullopt;
		}
		// No geometry here, so a region's label carries its number alone;
		// a raw term keeps the preset match TermLabel makes.
		const GeometryRow unread;
		for (auto& term : *terms) {
			term.recipe = ReadTermOver(term.text, a_presets, a_existing);
			term.label = Is<RawTerm>(term.recipe) ? TermLabel(term.text, a_presets, a_existing) : TermLabelOf(term.recipe, a_presets, unread);
		}
		return terms;
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

	// ------------------------------------------------------------ term templates

	std::string_view TermRecipeName(const TermRecipe& a_recipe) noexcept
	{
		return Match(
			a_recipe,
			[](const RawTerm&) { return "raw"; }, [](const ReferenceTerm&) { return "reference"; }, [](const ThresholdTerm&) { return "threshold"; },
			[](const WhatPresetTerm&) { return "preset"; }, [](const PartitionTerm&) { return "partition"; }, [](const BoneTerm&) { return "bones"; },
			[](const ComponentTerm&) { return "component"; }, [](const ClusterTerm&) { return "cluster"; });
	}

	namespace
	{
		// The channels a threshold can test: the scalar ones; diffuseRgb is a colour.
		constexpr std::array<MaterialChannel, 8> kThresholdChannels{ MaterialChannel::kDiffuseLuma, MaterialChannel::kNormalSlope, MaterialChannel::kRoughness, MaterialChannel::kMetallic,
			MaterialChannel::kOcclusion, MaterialChannel::kReflectance, MaterialChannel::kDisplacement, MaterialChannel::kRelief };

		[[nodiscard]] std::vector<std::string> ThresholdChannelNames()
		{
			std::vector<std::string> names;
			for (const auto channel : kThresholdChannels) {
				names.emplace_back(MaterialChannelName(channel));
			}
			return names;
		}

		// Numbers as the templates spell them: four decimals at most, the
		// form ParamText writes, so a number read back is the number written.
		[[nodiscard]] std::string NumberText(float a_value)
		{
			return ParamText(Param{ a_value });
		}

		// The whole text as one number, spaces around it allowed; nothing otherwise.
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

		// The whole text as a whole number in 0..a_max.
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

		// A setting as the text will carry it: a typed number rounded to the
		// spelling, so the recipe and its text agree.
		[[nodiscard]] float Spelled(float a_value)
		{
			return ReadNumber(NumberText(a_value)).value_or(a_value);
		}

		[[nodiscard]] int Percent(float a_share)
		{
			return static_cast<int>(std::lround(std::clamp(a_share, 0.0f, 1.0f) * 100.0f));
		}

		// The word a region's source goes by in labels and offers.
		[[nodiscard]] std::string_view RegionWord(RegionSource a_source) noexcept
		{
			return a_source == RegionSource::kChart ? "chart" : "part";
		}

		// The id map bake of a region source.
		[[nodiscard]] SourceKind RegionBakeOf(RegionSource a_source)
		{
			return a_source == RegionSource::kChart ? SourceKind{ BakeSource{ ChartIdBake{} } } : SourceKind{ BakeSource{ ComponentIdBake{} } };
		}

		[[nodiscard]] const MeshRegion* RegionOf(const GeometryRow& a_geometry, RegionSource a_source, std::uint16_t a_id)
		{
			for (const auto& region : a_geometry.regions) {
				if (region.source == a_source && region.id == a_id) {
					return &region;
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

		// ------------------------------------------------------------ spelling

		// The channel operand of a threshold: the source, quantised to P
		// levels when posterize > 1.
		[[nodiscard]] std::string OperandText(const std::string& a_name, std::uint8_t a_posterize)
		{
			const auto reference = ReferenceText(a_name);
			if (a_posterize > 1) {
				return std::format("floor({} * {}) / {}", reference, a_posterize, a_posterize);
			}
			return reference;
		}

		// smoothstep(c - s, c + s, X): the settings are written as themselves,
		// never summed, so ReadTerm gets them back exactly.
		[[nodiscard]] std::string EdgeText(float a_centre, float a_softness, const std::string& a_operand)
		{
			const auto centre = NumberText(a_centre);
			const auto softness = NumberText(a_softness);
			return std::format("smoothstep({} - {}, {} + {}, {})", centre, softness, centre, softness, a_operand);
		}

		// The threshold's text: the low edge times the complement of the high
		// edge, each omitted where it would be trivial (low 0, high 1); with
		// both trivial, step(0, X) keeps the channel readable; invert wraps.
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

		// ------------------------------------------------------------- reading

		// A left-to-right reader over the exact spellings the templates
		// write: each step consumes its text or fails where it stands, so a
		// read is one pass over the text and never recurses.
		class Cursor
		{
		public:
			explicit Cursor(std::string_view a_text) :
				text_(a_text)
			{}

			[[nodiscard]] bool Take(std::string_view a_literal)
			{
				if (!text_.substr(at_).starts_with(a_literal)) {
					return false;
				}
				at_ += a_literal.size();
				return true;
			}

			// A run of name characters, which must be a name.
			[[nodiscard]] std::optional<std::string> TakeName()
			{
				std::size_t end = at_;
				while (end < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[end])) || text_[end] == '_')) {
					++end;
				}
				const auto name = text_.substr(at_, end - at_);
				if (!IsName(name)) {
					return std::nullopt;
				}
				at_ = end;
				return std::string{ name };
			}

			// A run of number characters (a leading '-' allowed), read whole.
			[[nodiscard]] std::optional<float> TakeNumber()
			{
				std::size_t end = at_;
				if (end < text_.size() && text_[end] == '-') {
					++end;
				}
				while (end < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[end])) || text_[end] == '.')) {
					++end;
				}
				const auto number = ReadNumber(text_.substr(at_, end - at_));
				if (number) {
					at_ = end;
				}
				return number;
			}

			[[nodiscard]] std::optional<std::uint32_t> TakeWhole(std::uint32_t a_max)
			{
				std::size_t end = at_;
				while (end < text_.size() && std::isdigit(static_cast<unsigned char>(text_[end]))) {
					++end;
				}
				const auto number = ReadWhole(text_.substr(at_, end - at_), a_max);
				if (number) {
					at_ = end;
				}
				return number;
			}

			[[nodiscard]] bool Done() const noexcept { return at_ == text_.size(); }

		private:
			std::string_view text_;
			std::size_t      at_ = 0;
		};

		// The operand of a threshold as read: which source and how quantised.
		struct Operand
		{
			std::string        name;
			std::uint8_t       posterize = 0;
			[[nodiscard]] bool operator==(const Operand&) const = default;
		};

		[[nodiscard]] std::optional<Operand> TakeOperand(Cursor& a_cursor)
		{
			Operand operand;
			if (a_cursor.Take("floor(@")) {
				const auto name = a_cursor.TakeName();
				if (!name || !a_cursor.Take(" * ")) {
					return std::nullopt;
				}
				const auto levels = a_cursor.TakeWhole(255);
				if (!levels || *levels < 2 || !a_cursor.Take(") / ")) {
					return std::nullopt;
				}
				const auto again = a_cursor.TakeWhole(255);
				if (!again || *again != *levels) {
					return std::nullopt;
				}
				operand.name = *name;
				operand.posterize = static_cast<std::uint8_t>(*levels);
				return operand;
			}
			if (!a_cursor.Take("@")) {
				return std::nullopt;
			}
			const auto name = a_cursor.TakeName();
			if (!name) {
				return std::nullopt;
			}
			operand.name = *name;
			return operand;
		}

		// One edge as EdgeText writes it; the operand must be the one already
		// read when there is one.
		struct Edge
		{
			float   centre = 0.0f;
			float   softness = 0.0f;
			Operand operand;
		};

		[[nodiscard]] std::optional<Edge> TakeEdge(Cursor& a_cursor)
		{
			Edge edge;
			if (!a_cursor.Take("smoothstep(")) {
				return std::nullopt;
			}
			const auto centre = a_cursor.TakeNumber();
			if (!centre || !a_cursor.Take(" - ")) {
				return std::nullopt;
			}
			const auto softness = a_cursor.TakeNumber();
			if (!softness || !a_cursor.Take(", ")) {
				return std::nullopt;
			}
			const auto centreAgain = a_cursor.TakeNumber();
			if (!centreAgain || *centreAgain != *centre || !a_cursor.Take(" + ")) {
				return std::nullopt;
			}
			const auto softnessAgain = a_cursor.TakeNumber();
			if (!softnessAgain || *softnessAgain != *softness || !a_cursor.Take(", ")) {
				return std::nullopt;
			}
			const auto operand = TakeOperand(a_cursor);
			if (!operand || !a_cursor.Take(")")) {
				return std::nullopt;
			}
			edge.centre = *centre;
			edge.softness = *softness;
			edge.operand = *operand;
			return edge;
		}

		// A threshold's text read back, with the source it tests; the channel
		// is the caller's to look up.
		struct ThresholdRead
		{
			ThresholdTerm term;
			std::string   name;
		};

		[[nodiscard]] std::optional<ThresholdRead> ReadThreshold(std::string_view a_text)
		{
			Cursor        cursor(a_text);
			ThresholdRead read;
			read.term.invert = cursor.Take("1 - (");
			std::optional<Operand> operand;
			if (cursor.Take("step(0, ")) {
				operand = TakeOperand(cursor);
				if (!operand || !cursor.Take(")")) {
					return std::nullopt;
				}
				read.term.low = 0.0f;
				read.term.high = 1.0f;
				read.term.softness = ThresholdTerm{}.softness;
			} else if (cursor.Take("1 - ")) {
				const auto high = TakeEdge(cursor);
				if (!high) {
					return std::nullopt;
				}
				operand = high->operand;
				read.term.low = 0.0f;
				read.term.high = high->centre;
				read.term.softness = high->softness;
			} else {
				const auto low = TakeEdge(cursor);
				if (!low) {
					return std::nullopt;
				}
				operand = low->operand;
				read.term.low = low->centre;
				read.term.high = 1.0f;
				read.term.softness = low->softness;
				if (cursor.Take(" * (1 - ")) {
					const auto high = TakeEdge(cursor);
					if (!high || high->operand != *operand || high->softness != low->softness || !cursor.Take(")")) {
						return std::nullopt;
					}
					read.term.high = high->centre;
				}
			}
			if (read.term.invert && !cursor.Take(")")) {
				return std::nullopt;
			}
			if (!cursor.Done()) {
				return std::nullopt;
			}
			read.term.posterize = operand->posterize;
			read.name = operand->name;
			return read;
		}

		// "abs(@name * 255 - ID) < 0.5" read back: the source and the id.
		struct RegionRead
		{
			std::string   name;
			std::uint32_t id = 0;
		};

		[[nodiscard]] std::optional<RegionRead> ReadRegion(std::string_view a_text)
		{
			Cursor cursor(a_text);
			if (!cursor.Take("abs(@")) {
				return std::nullopt;
			}
			const auto name = cursor.TakeName();
			if (!name || !cursor.Take(" * 255 - ")) {
				return std::nullopt;
			}
			const auto id = cursor.TakeWhole(kMaxRegions);
			if (!id || !cursor.Take(") < 0.5") || !cursor.Done()) {
				return std::nullopt;
			}
			return RegionRead{ *name, *id };
		}

		[[nodiscard]] const SourceKind* KindNamed(const Existing& a_existing, std::string_view a_name)
		{
			for (const auto& [name, kind] : a_existing.sources) {
				if (name == a_name) {
					return &kind;
				}
			}
			return nullptr;
		}

		[[nodiscard]] const RegionPreset* WhatPresetNamed(const Presets& a_presets, std::string_view a_name)
		{
			const auto it = std::ranges::find(a_presets.what, a_name, &RegionPreset::name);
			return it == a_presets.what.end() ? nullptr : &*it;
		}

		// The template a text fits over what the recipe has; RawTerm when none.
		TermRecipe ReadTermOver(std::string_view a_text, const Presets& a_presets, const Existing& a_existing)
		{
			if (a_text.empty() || a_text.size() > kMaxExpressionLength) {
				return RawTerm{};
			}
			// A lone reference: a partition or bone bake by its definition, else the name.
			if (a_text.front() == '@' && IsName(a_text.substr(1))) {
				const auto  name = std::string{ a_text.substr(1) };
				const auto* kind = KindNamed(a_existing, name);
				const auto* bake = kind ? Get<BakeSource>(*kind) : nullptr;
				if (bake) {
					if (const auto* partition = Get<PartitionBake>(bake->bake)) {
						return PartitionTerm{ partition->slot };
					}
					if (const auto* bones = Get<BoneWeightBake>(bake->bake)) {
						return BoneTerm{ bones->bones };
					}
				}
				return ReferenceTerm{ name };
			}
			if (const auto region = ReadRegion(a_text)) {
				const auto* kind = KindNamed(a_existing, region->name);
				if (kind) {
					if (const auto* clusters = Get<MaterialClustersSource>(*kind); clusters && region->id <= kMaxClusters) {
						return ClusterTerm{ SettingsOf(*clusters), static_cast<std::uint8_t>(region->id) };
					}
					if (const auto* bake = Get<BakeSource>(*kind)) {
						if (Is<ComponentIdBake>(bake->bake)) {
							return ComponentTerm{ RegionSource::kComponent, static_cast<std::uint16_t>(region->id) };
						}
						if (Is<ChartIdBake>(bake->bake)) {
							return ComponentTerm{ RegionSource::kChart, static_cast<std::uint16_t>(region->id) };
						}
					}
				}
			}
			if (const auto threshold = ReadThreshold(a_text)) {
				const auto* kind = KindNamed(a_existing, threshold->name);
				const auto* material = kind ? Get<MaterialSource>(*kind) : nullptr;
				if (material && std::ranges::find(kThresholdChannels, material->channel) != kThresholdChannels.end()) {
					ThresholdTerm term = threshold->term;
					term.channel = material->channel;
					return term;
				}
			}
			for (const auto& preset : a_presets.what) {
				const auto term = MaterialiseTerm(preset, a_existing);
				if (term.edits.empty() && term.expression == a_text) {
					return WhatPresetTerm{ preset.name };
				}
			}
			return RawTerm{};
		}
	}

	PresetTerm BuildTerm(const TermRecipe& a_recipe, const Presets& a_presets, const Existing& a_existing)
	{
		SourceNamer namer(a_existing);
		std::string text = Match(
			a_recipe,
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
			[&](const ComponentTerm& t) {
				const auto name = namer.NameFor(t.source == RegionSource::kChart ? "charts" : "components", RegionBakeOf(t.source));
				return RegionText(name, t.id);
			},
			[&](const ClusterTerm& t) {
				const auto name = namer.NameFor("clusters", SourceOf(t.settings));
				return RegionText(name, t.id);
			});
		return PresetTerm{ std::move(namer).Edits(), std::move(text) };
	}

	TermRecipe ReadTerm(std::string_view a_text, const Presets& a_presets, const RecipeRow& a_recipe)
	{
		return ReadTermOver(a_text, a_presets, ExistingOf(a_recipe));
	}

	std::string TermLabelOf(const TermRecipe& a_recipe, const Presets& a_presets, const GeometryRow& a_geometry)
	{
		return Match(
			a_recipe,
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
			[&](const ComponentTerm& t) {
				const auto* region = RegionOf(a_geometry, t.source, t.id);
				if (!region) {
					return std::format("{} {}", RegionWord(t.source), t.id);
				}
				if (region->dominantBone.empty()) {
					return std::format("{} {}: {}%", RegionWord(t.source), t.id, Percent(region->share));
				}
				return std::format("{} {}: {}, {}%", RegionWord(t.source), t.id, PlainBoneName(a_presets, region->dominantBone), Percent(region->share));
			},
			[&](const ClusterTerm& t) {
				const auto* cluster = ClusterOf(a_geometry, t.id);
				if (!cluster) {
					return std::format("material {}", t.id);
				}
				return std::format("material {}: {}, {}%", t.id, cluster->description, Percent(cluster->share));
			});
	}

	// ------------------------------------------------------------------- forms

	namespace
	{
		[[nodiscard]] FieldSpec Spec(std::string a_name, FieldKind a_kind, std::string a_text, std::vector<std::string> a_names = {})
		{
			FieldSpec field;
			field.name = std::move(a_name);
			field.kind = a_kind;
			field.text = std::move(a_text);
			field.names = std::move(a_names);
			return field;
		}

		// A field whose committed text sets one setting of a copy of the
		// recipe through a_set, which refuses text that does not parse.
		template <class Recipe>
		[[nodiscard]] TermField Setting(const Recipe& a_recipe, FieldSpec a_field, std::function<bool(Recipe&, const std::string&)> a_set)
		{
			return TermField{ std::move(a_field), [a_recipe, a_set](const std::string& a_text) -> std::optional<TermRecipe> {
				Recipe edited = a_recipe;
				return a_set(edited, a_text) ? std::optional<TermRecipe>{ edited } : std::nullopt;
			} };
		}

		// A number in a range as a setting.
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

		// Comma-separated names, trimmed, empties dropped; nothing when none
		// remain or the list is past the bone cap.
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
				if (!channel || std::ranges::find(kThresholdChannels, *channel) == kThresholdChannels.end()) {
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

		std::vector<TermField> ComponentForm(const ComponentTerm& a_term, const Presets& a_presets, const GeometryRow& a_geometry)
		{
			// The choice offers each region of the source by its label; a
			// committed label picks that region, a bare number that id.
			std::vector<std::string>   labels;
			std::vector<std::uint16_t> ids;
			for (const auto& region : a_geometry.regions) {
				if (region.source == a_term.source) {
					labels.push_back(TermLabelOf(ComponentTerm{ a_term.source, region.id }, a_presets, a_geometry));
					ids.push_back(region.id);
				}
			}
			std::vector<TermField> form;
			form.push_back(Setting<ComponentTerm>(a_term, Spec("id", FieldKind::kChoice, TermLabelOf(a_term, a_presets, a_geometry), labels), [labels, ids](ComponentTerm& t, const std::string& a_text) {
				const auto it = std::ranges::find(labels, a_text);
				if (it != labels.end()) {
					const auto index = static_cast<std::size_t>(it - labels.begin());
					t.id = index < ids.size() ? ids[index] : t.id;
					return index < ids.size();
				}
				const auto id = ReadWhole(a_text, kMaxRegions);
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

		std::vector<TermField> PartitionForm(const PartitionTerm& a_term, const Presets& a_presets, const GeometryRow& a_geometry)
		{
			// The choice offers the geometry's partitions by plain name; a
			// committed text is one of those, else a biped slot's name or a number 30..61.
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

	std::vector<TermField> TermForm(const TermRecipe& a_recipe, const Presets& a_presets, const GeometryRow& a_geometry)
	{
		return Match(
			a_recipe,
			[](const RawTerm&) { return std::vector<TermField>{}; },
			[](const ReferenceTerm&) { return std::vector<TermField>{}; },
			[](const WhatPresetTerm&) { return std::vector<TermField>{}; },
			[](const ThresholdTerm& t) { return ThresholdForm(t); },
			[](const ClusterTerm& t) { return ClusterForm(t); },
			[&](const ComponentTerm& t) { return ComponentForm(t, a_presets, a_geometry); },
			[](const BoneTerm& t) { return BoneForm(t); },
			[&](const PartitionTerm& t) { return PartitionForm(t, a_presets, a_geometry); });
	}

	// ------------------------------------------------------------------ offers

	namespace
	{
		[[nodiscard]] TermOffer Offer(std::string_view a_group, std::string a_name, std::string a_detail, TermRecipe a_recipe)
		{
			TermOffer offer;
			offer.group = std::string{ a_group };
			offer.name = std::move(a_name);
			offer.detail = std::move(a_detail);
			offer.recipe = std::move(a_recipe);
			return offer;
		}

		// A group the piece cannot offer yet: one row carrying the reason.
		[[nodiscard]] TermOffer Unavailable(std::string_view a_group, std::string a_reason, TermRecipe a_recipe)
		{
			TermOffer offer = Offer(a_group, std::string{ a_group }, {}, std::move(a_recipe));
			offer.unavailable = std::move(a_reason);
			return offer;
		}

		constexpr std::string_view kMeshUnread = "the mesh has not been read yet";
		constexpr std::string_view kNoClusters = "the material has no clusters yet";
	}

	std::vector<TermOffer> OffersOf(const Presets& a_presets, const RecipeRow& a_recipe, const GeometryRow& a_geometry, std::string_view a_editing)
	{
		std::vector<TermOffer> offers;
		// parts
		if (!a_geometry.meshRead) {
			offers.push_back(Unavailable(kOfferGroups[0], std::string{ kMeshUnread }, ComponentTerm{}));
		}
		for (const auto& region : a_geometry.regions) {
			const ComponentTerm term{ region.source, region.id };
			std::string         detail = std::format("{}% of the mesh", Percent(region.share));
			if (!region.dominantBone.empty()) {
				detail += std::format(", {} {}%", PlainBoneName(a_presets, region.dominantBone), Percent(region.dominantShare));
			}
			offers.push_back(Offer(kOfferGroups[0], std::format("{} {}", RegionWord(region.source), region.id), std::move(detail), term));
		}
		// materials
		if (a_geometry.clusters.empty()) {
			offers.push_back(Unavailable(kOfferGroups[1], std::string{ kNoClusters }, ClusterTerm{}));
		}
		for (const auto& cluster : a_geometry.clusters) {
			offers.push_back(Offer(kOfferGroups[1], std::format("material {}", cluster.id), std::format("{}, {}%", cluster.description, Percent(cluster.share)), ClusterTerm{ ClusterSettings{}, cluster.id }));
		}
		// bones
		for (const auto& bone : a_geometry.bones) {
			offers.push_back(Offer(kOfferGroups[2], PlainBoneName(a_presets, bone.name), std::format("{}% of the mesh", Percent(bone.coverage)), BoneTerm{ { bone.name } }));
		}
		// partitions
		for (const auto& partition : a_geometry.partitions) {
			offers.push_back(Offer(kOfferGroups[3], PlainPartitionName(a_presets, partition.slot), std::format("{} triangles", partition.triangles), PartitionTerm{ partition.slot }));
		}
		// channels
		for (const auto channel : kThresholdChannels) {
			ThresholdTerm term;
			term.channel = channel;
			term.low = 0.5f;
			term.high = 1.0f;
			offers.push_back(Offer(kOfferGroups[4], std::string{ MaterialChannelName(channel) }, "0.5..1", term));
		}
		// presets
		for (const auto& preset : a_presets.what) {
			offers.push_back(Offer(kOfferGroups[5], preset.name, preset.expression, WhatPresetTerm{ preset.name }));
		}
		// masks
		for (const auto& mask : a_recipe.maskRows) {
			if (mask.name == kScratchMask || mask.name == a_editing) {
				continue;
			}
			offers.push_back(Offer(kOfferGroups[6], mask.name, mask.text, ReferenceTerm{ mask.name }));
		}
		// sources
		for (const auto& source : a_recipe.sourceRows) {
			const auto kind = SourceKindOf(source);
			offers.push_back(Offer(kOfferGroups[7], source.name, kind ? DescribeSource(*kind) : source.kind, ReferenceTerm{ source.name }));
		}
		return offers;
	}

	std::string TermDetailOf(const Term& a_term, std::span<const TermOffer> a_offers)
	{
		for (const auto& offer : a_offers) {
			if (offer.recipe == a_term.recipe) {
				return offer.detail;
			}
		}
		return Match(
			a_term.recipe,
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
			[&](const ComponentTerm& t) { return std::format("{} {}", RegionSourceName(t.source), t.id); },
			[&](const ReferenceTerm& t) { return ReferenceText(t.name); },
			[&](const WhatPresetTerm& t) { return t.preset; },
			[&](const PartitionTerm& t) { return std::format("partition {}", t.slot); },
			[&](const BoneTerm& t) { return std::to_string(t.bones.size()) + " bone(s)"; },
			[&](const RawTerm&) { return a_term.text; });
	}

	// ------------------------------------------------------------ paint recipe

	MaterialOutput PaintOutput(Surface a_surface)
	{
		MaterialOutput output = DefaultOutput(a_surface, Slot::kEmissive);
		Layer          layer = DefaultLayer();
		layer.mask = Ref{ std::string{ kScratchMask } };
		output.stack = { std::move(layer) };
		return output;
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
		std::vector<std::string> taken;
		for (const auto& source : a_active.sources) {
			taken.push_back(source.name);
		}
		for (const auto& mask : a_active.masks) {
			taken.push_back(mask.name);
		}
		for (const auto& read : program->References()) {
			const auto* source = a_paint.FindSource(read);
			if (!source) {
				continue;  // a mask of the active recipe, or a signal: already there
			}
			const auto* same = a_active.FindSource(read);
			if (same && same->kind == source->kind) {
				continue;
			}
			// The same definition under another name, else a new row under
			// the paint's name made unique.
			std::string to;
			for (const auto& candidate : a_active.sources) {
				if (candidate.kind == source->kind) {
					to = candidate.name;
					break;
				}
			}
			if (to.empty()) {
				to = UniqueName(read, taken);
				taken.push_back(to);
				edits.push_back(AddSource{ to, source->kind });
			}
			if (to != read) {
				text = RenameInExpression(text, read, to, false);
			}
		}
		if (!a_active.FindMask(a_name)) {
			edits.push_back(AddMask{ std::string{ a_name } });
		}
		edits.push_back(SetMask{ std::string{ a_name }, std::move(text) });
		return edits;
	}
}

namespace WornEnchantmentPBR::Studio
{
	MeshFacts FactsOf(const MeshData& a_mesh)
	{
		return MeshFacts{ PartitionsOf(a_mesh), BonesOf(a_mesh) };
	}
}
