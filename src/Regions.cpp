#include "Regions.h"

#include "Studio.h"

#include <nlohmann/json.hpp>

#include "Expression.h"

#include <algorithm>
#include <cctype>
#include <format>

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

	PresetTerm MaterialiseTerm(const RegionPreset& a_preset, const Existing& a_existing)
	{
		PresetTerm               term;
		auto&                    edits = term.edits;
		std::vector<std::string> taken = a_existing.taken;

		// The name the expression will read a definition by: an existing twin's
		// name, else a new row named as the preset says, made unique.
		const auto nameFor = [&](const std::string& a_wanted, const SourceKind& a_kind) {
			for (const auto& [name, kind] : a_existing.sources) {
				if (kind == a_kind) {
					return name;
				}
			}
			const auto name = UniqueName(a_wanted, taken);
			taken.push_back(name);
			edits.push_back(AddSource{ name, a_kind });
			return name;
		};

		std::string expression;
		if (a_preset.kind == PresetKind::kWhere) {
			std::vector<std::string> terms;
			if (a_preset.partition) {
				terms.push_back(ReferenceText(nameFor("partition", BakeSource{ PartitionBake{ *a_preset.partition } })));
			}
			if (!a_preset.bones.empty()) {
				terms.push_back(ReferenceText(nameFor("bones", BakeSource{ BoneWeightBake{ a_preset.bones } })));
			}
			for (const auto& term : terms) {
				expression += (expression.empty() ? "" : " * ") + term;
			}
		} else {
			expression = a_preset.expression;
			for (const auto& [wanted, kind] : a_preset.sources) {
				const auto name = nameFor(wanted, kind);
				if (name != wanted) {
					expression = RenameInExpression(expression, wanted, name, false);
				}
			}
		}
		term.expression = expression;
		return term;
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

	std::optional<std::vector<Term>> TermsOfMask(std::string_view a_text, const Presets& a_presets, const Existing& a_existing)
	{
		auto terms = ParseRegion(a_text);
		if (!terms) {
			return std::nullopt;
		}
		for (auto& term : *terms) {
			term.label = TermLabel(term.text, a_presets, a_existing);
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
			std::string part = term.label;
			if (!name.empty()) {
				part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
			}
			name += part;
		}
		return IsName(name) ? name : "region";
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
