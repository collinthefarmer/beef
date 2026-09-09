#include "Importer.h"
#include "Signals.h"
#include "test_support.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace
{
	class RecordEnvironment final : public SignalEnvironment
	{
	public:
		explicit RecordEnvironment(const EffectShaderRecord& a_record) :
			record_(a_record) {}
		float                               ActorValue(std::string_view, Measure) const override { return 0.0f; }
		float                               ActorState(ActorStateKind) const override { return 0.0f; }
		float                               Enchantment(EnchantmentField) const override { return 0.0f; }
		std::optional<Timing::EffectParams> EffectShader(const FormRef& a_ref) const override
		{
			return a_ref == record_.Reference() ? std::optional{ record_.params } : std::nullopt;
		}

	private:
		const EffectShaderRecord& record_;
	};

	std::string Errors(const std::vector<Diagnostic>& a_diags)
	{
		std::string out;
		for (const auto& d : a_diags) {
			if (d.severity == Severity::kError) {
				out += std::format("[{}: {}] ", d.where, d.message);
			}
		}
		return out;
	}

	void SpotChecks(const EffectShaderRecord& a_record, const Recipe& a_recipe)
	{
		const auto& id = a_recipe.id;
		Check(a_recipe.keys.size() == 1 && a_recipe.keys[0].kind == KeyKind::kEffectShader && a_recipe.keys[0].Form() && a_recipe.keys[0].Form()->text == a_record.editorId, id + ": keyed by the effect shader's editor ID");
		Check(a_recipe.metadata.imported == "BetterEnchantmentEffects 0.1.0" && !a_recipe.metadata.description.empty(), id + ": imported and description set");

		const bool  hasFill = !a_record.fillTexture.empty();
		const auto* fill = a_recipe.FindSource("fill");
		const auto* image = fill ? Get<ImageSource>(fill->kind) : nullptr;
		if (hasFill) {
			Check(image && image->path == a_record.fillTexture, id + ": fill field is the record's texture");
			const auto* tile = image && image->tile ? Get<std::array<Param, 2>>(*image->tile) : nullptr;
			Check(tile && Near(std::get<float>((*tile)[0]), a_record.tileU) && Near(std::get<float>((*tile)[1]), a_record.tileV), id + ": tiling from the record");
			Check(image && image->scroll && Get<Ref>(*image->scroll) && Get<Ref>(*image->scroll)->name == "scroll", id + ": scrolled by the record's scroll signal");
		} else {
			Check(!fill && !a_recipe.FindSource("sheenField") && !a_recipe.FindSource("glossField"), id + ": a record without a fill texture has no fields");
			const auto* emissive = Get<SurfaceOutput>(a_recipe.outputs[0]);
			Check(emissive && emissive->stack.size() == 1 && Is<Vec3>(emissive->stack[0].source), id + ": its glow is a flat colour");
		}

		const auto* rest = a_recipe.FindCurve("rest");
		Check(rest && rest->text == std::format("x / {}", Timing::BaselineAlpha(a_record.params.fill)), id + ": rest curve divides by the record's baseline: " + (rest ? rest->text : ""));

		const auto graph = SignalGraph::Compile(a_recipe.signals, a_recipe.curves);
		Check(graph.Diagnostics().empty(), id + ": signal graph compiles cleanly");
		SignalState       state(graph);
		RecordEnvironment env(a_record);
		state.Tick(env, { 0.0f, 0.0f });
		const float level = Timing::EvaluateAlpha(a_record.params.fill, 0.0f, 1.0f) / Timing::BaselineAlpha(a_record.params.fill);
		const float edgeLevel = Timing::EvaluateAlpha(a_record.params.edge, 0.0f, 1.0f) / Timing::BaselineAlpha(a_record.params.edge);
		const float pulse = std::clamp(level, 0.0f, 2.0f);
		Check(Near(state.Scalar("fillLevel"), level, 1e-3f) && Near(state.Scalar("glowLevel"), level, 1e-3f), std::format("{}: level {} at t=0 from the record", id, level));
		Check(Near(state.Scalar("sheenWeight"), std::clamp(edgeLevel * 0.5f, 0.0f, 1.0f), 1e-3f), id + ": sheen weight is edge level x sheen scale");
		Check(Near(state.Scalar("inflate"), (1.0f + pulse) * 0.01f, 1e-4f), id + ": inflation 1% rest + 1% per unit of pulse");
		Check(Near(state.Scalar("shellOpacity"), std::clamp(0.5f * pulse, 0.0f, 1.0f), 1e-3f), id + ": shell opacity");
		Check(Near(state.Scalar("heightScale"), 0.1f * pulse, 1e-3f), id + ": shimmer scale");
		Check(TypeOf(state.ValueOf("scroll")) == ValueType::kVec2, id + ": scroll is a vec2");
		if (hasFill) {
			Check(Near(state.Scalar("glossAmount"), 0.25f, 1e-3f) && TypeOf(state.ValueOf("sheenScroll")) == ValueType::kVec2, id + ": gloss amount and sheen scroll");
		}

		int shellEmissive = 0, shellFuzz = 0, materialHeight = 0, materialRmaos = 0, lights = 0;
		for (const auto& o : a_recipe.outputs) {
			Match(
				o,
				[&](const SurfaceOutput& m) {
					shellEmissive += m.surface == Surface::kShell && m.slot == Slot::kEmissive;
					shellFuzz += m.surface == Surface::kShell && m.slot == Slot::kFuzz;
					materialHeight += m.surface == Surface::kMaterial && m.slot == Slot::kHeight;
					materialRmaos += m.surface == Surface::kMaterial && m.slot == Slot::kRmaos;
				},
				[&](const LightOutput&) { ++lights; });
		}
		Check(shellEmissive == 1 && shellFuzz == 1 && materialHeight == 1 && materialRmaos == (hasFill ? 1 : 0) && lights == 1, id + ": default target policy");
		const auto* emissive = Get<SurfaceOutput>(a_recipe.outputs[0]);
		Check(emissive && emissive->stack.size() == 1 && emissive->stack[0].mask && emissive->stack[0].mask->name == "metal", id + ": emissive layer masked by metal");
		Check(a_recipe.shell.material == ShellMaterial::kPbrCopy && a_recipe.shell.blend == ShellBlend::kAdditive && a_recipe.shell.depthBias, id + ": PBR-copy additive shell");
		Check(IsAnimated(a_recipe, a_recipe.outputs[0]) == hasFill && IsAnimated(a_recipe, a_recipe.outputs[2]) == hasFill, id + ": field stacks are animated, flat ones static");
	}

	void KnownHues(const Recipe& a_recipe)
	{
		const auto* hue = a_recipe.FindSignal("glowHue");
		const auto* c = hue ? Get<ConstantSignal>(hue->kind) : nullptr;
		const auto* rgb = c ? Get<Vec3>(c->value) : nullptr;
		if (!rgb) {
			Check(false, a_recipe.id + ": glow hue is a constant colour");
			return;
		}
		if (a_recipe.id == "EnchArmorMagickaFXS") {
			Check(Near(rgb->x, 0.155f, 1e-3f) && Near(rgb->y, 0.388f, 1e-3f) && Near(rgb->z, 1.0f, 1e-3f), "magicka hue matches the POC log (0.155, 0.388, 1.000)");
		} else if (a_recipe.id == "EnchArmorStaminaFXS") {
			Check(Near(rgb->x, 0.380f, 1e-3f) && Near(rgb->y, 1.0f, 1e-3f) && Near(rgb->z, 0.468f, 1e-3f), "stamina hue matches the POC log (0.380, 1.000, 0.468)");
		} else if (a_recipe.id == "EnchArmorHealthFXS") {
			Check(Near(rgb->x, 1.0f, 1e-3f) && Near(rgb->y, 0.029f, 1e-3f) && Near(rgb->z, 0.029f, 1e-3f), "health hue matches the POC log (1.000, 0.029, 0.029)");
		} else if (a_recipe.id == "EnchArmorFireFXS") {
			Check(Near(rgb->x, 1.0f) && Near(rgb->y, 1.0f) && Near(rgb->z, 1.0f), "fire (palette record, white edge) imports as a white glow");
		}
	}
}

int main(int argc, char** argv)
{
	const bool update = argc > 1 && std::strcmp(argv[1], "--update") == 0;
	const auto efshDir = test::Fixtures() / "efsh";
	const auto recipeDir = test::Fixtures() / "recipes";
	std::error_code ec;
	std::filesystem::create_directories(recipeDir, ec);

	std::vector<std::filesystem::path> fixtures;
	for (const auto& entry : std::filesystem::directory_iterator(efshDir, ec)) {
		if (entry.path().extension() == ".json") {
			fixtures.push_back(entry.path());
		}
	}
	std::ranges::sort(fixtures);
	Check(fixtures.size() >= 7, std::format("the six vanilla EnchArmor*FXS fixtures and WaterBreathingFXS under {}", efshDir.string()));

	{
		EffectShaderRecord anonymous;
		anonymous.key = *FormKey::Parse("0x92DED~Skyrim.esm");
		Check(RecipeIdFor(anonymous) == "Skyrim-92DED" && anonymous.Reference().text == "0x92DED~Skyrim.esm", "records without an editor ID are named by form key");
	}

	for (const auto& path : fixtures) {
		const auto record = ParseEffectShaderRecord(test::ReadFile(path));
		Check(record.has_value(), std::format("{} parses: {}", path.filename().string(), record ? "" : record.error()));
		if (!record) {
			continue;
		}
		const auto recipe = ImportEffectShader(*record);
		const auto diags = Validate(recipe);
		Check(std::ranges::none_of(diags, [](const Diagnostic& d) { return d.severity == Severity::kError; }), recipe.id + ": imported recipe validates: " + Errors(diags));
		SpotChecks(*record, recipe);
		KnownHues(recipe);

		const auto text = SerializeRecipe(recipe);
		const auto loaded = ParseRecipe(text, recipe.id);
		Check(loaded.recipe && *loaded.recipe == recipe && !loaded.HasErrors(), recipe.id + ": file reads back identical: " + Errors(loaded.diagnostics));

		const auto expectedPath = recipeDir / (recipe.id + ".json");
		if (update || !std::filesystem::exists(expectedPath)) {
			Check(test::WriteFile(expectedPath, text), std::format("{}: wrote expected recipe {}", recipe.id, expectedPath.string()));
			std::printf("generated %s\n", expectedPath.string().c_str());
		} else {
			Check(test::ReadFile(expectedPath) == text, recipe.id + ": matches the checked-in expected recipe (run with --update after an intended change)");
		}
	}
	return test::Finish("importer");
}
