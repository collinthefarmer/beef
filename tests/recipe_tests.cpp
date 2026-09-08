// Recipe format 1: forms, globs, the JSON round trip of the canonical file
// and of a recipe holding one of everything, every row-level error with its
// `where`, validation rules, resolution, variants, and static/animated
// classification.

#include "Recipe.h"
#include "Vocabulary.h"
#include "test_support.h"

#include <algorithm>
#include <format>
#include <set>
#include <utility>

using namespace WornEnchantmentPBR;
using test::Check;
using test::Near;

namespace
{
	FormRef Form(const char* a_text)
	{
		return FormRef::From(a_text);
	}

	FormKey Key(const char* a_text)
	{
		return *FormKey::Parse(a_text);
	}

	Signal Const(const char* a_name, float a_value)
	{
		return Signal{ a_name, ConstantSignal{ a_value }, std::nullopt };
	}

	Signal Colour(const char* a_name, Vec3 a_value)
	{
		return Signal{ a_name, ConstantSignal{ a_value }, std::nullopt };
	}

	Signal Expr(const char* a_name, const char* a_text)
	{
		return Signal{ a_name, ExprSignal{ a_text }, std::nullopt };
	}

	Ref At(const char* a_name)
	{
		return Ref{ a_name };
	}

	std::string Errors(const std::vector<Diagnostic>& a_diags, bool a_warnings = false)
	{
		std::string out;
		for (const auto& d : a_diags) {
			if (d.severity == Severity::kError || a_warnings) {
				out += std::format("[{} {}: {}] ", d.severity == Severity::kError ? "error" : "warn", d.where, d.message);
			}
		}
		return out;
	}

	bool HasError(const std::vector<Diagnostic>& a_diags, std::string_view a_where, std::string_view a_fragment)
	{
		return std::ranges::any_of(a_diags, [&](const Diagnostic& d) {
			return d.where.find(a_where) != std::string::npos && d.message.find(a_fragment) != std::string::npos;
		});
	}

	bool NoErrors(const std::vector<Diagnostic>& a_diags)
	{
		return std::ranges::none_of(a_diags, [](const Diagnostic& d) { return d.severity == Severity::kError; });
	}

	// One of everything, so a JSON round trip exercises every branch.
	Recipe Everything()
	{
		Recipe r;
		r.id = "everything";
		r.metadata.name = "Everything";
		r.metadata.author = "tests";
		r.metadata.description = "one of each row";
		r.metadata.version = "1.0";
		r.metadata.imported = "WornEnchantmentPBR 0.1.0";
		r.metadata.meta = R"json({"nexusId":12345,"notes":"kept verbatim"})json";
		r.keys = { RecipeKey{ KeyKind::kMagicEffect, Form("EnchFortifyHealthConstantSelf"), {} }, RecipeKey{ KeyKind::kEnchantment, Form("0x49509~Skyrim.esm"), {} },
			RecipeKey{ KeyKind::kEffectShader, Form("EnchArmorMagickaFXS"), {} }, RecipeKey{ KeyKind::kKeyword, Form("ArmorMaterialEbony"), {} },
			RecipeKey{ KeyKind::kMaterial, {}, "armor/iron/*" }, RecipeKey{ KeyKind::kArmor, Form("0x12E49~Skyrim.esm"), {} }, RecipeKey{ KeyKind::kDefault, {}, {} } };
		r.priority = 42;
		r.clock.speed = 1.5f;

		r.signals.push_back(Const("one", 1.0f));
		r.signals.push_back(Colour("red", Vec3{ 1, 0, 0 }));
		r.signals.push_back(Signal{ "pair", ConstantSignal{ Vec2{ 0.25f, 0.5f } }, std::nullopt });
		r.signals.push_back(Signal{ "pulse", PulseSignal{ 0.25f, At("one"), 2.0f, 0.1f, Waveform::kTriangle }, std::nullopt });
		r.signals.push_back(Signal{ "ramp", RampSignal{ 0.0f, 1.0f, 3.0f }, CurveRef{ "1 - x" } });
		r.signals.push_back(Signal{ "efshAlpha", EfshSignal{ EfshField::kFillAlpha, Form("EnchArmorMagickaFXS") }, CurveRef{ "@rest" } });
		r.signals.push_back(Signal{ "efshEdge", EfshSignal{ EfshField::kEdgeColor, Form("EnchArmorMagickaFXS") }, std::nullopt });
		r.signals.push_back(Signal{ "scroll", EfshSignal{ EfshField::kScroll, Form("EnchArmorMagickaFXS") }, std::nullopt });
		r.signals.push_back(Signal{ "health", ActorValueSignal{ "Health", Measure::kCurrent }, std::nullopt });
		r.signals.push_back(Signal{ "healthMax", ActorValueSignal{ "Health", Measure::kMax }, std::nullopt });
		r.signals.push_back(Signal{ "combat", ActorStateSignal{ ActorStateKind::kInCombat }, std::nullopt });
		r.signals.push_back(Signal{ "magnitude", EnchantmentSignal{ EnchantmentField::kMagnitude }, CurveRef{ "smoothstep(0, 100, x)" } });
		r.signals.push_back(Signal{ "hit", TriggerSignal{ EventSource{ "hit.received", EventFilter{ "NPC Spine*", "", ValueRange{ 10.0f, std::nullopt } }, "NPC Spine2 [Spn2]" }, 1.5f, 3 }, CurveRef{ "@flash" } });
		r.signals.push_back(Signal{ "surge", TriggerSignal{ PluginSource{ "MyMod.Surge" }, 2.0f, 1 }, std::nullopt });
		r.signals.push_back(Signal{ "wound", DeltaSignal{ At("damage") }, std::nullopt });
		r.signals.push_back(Signal{ "damage", ActorValueSignal{ "Health", Measure::kDamage }, std::nullopt });
		r.signals.push_back(Signal{ "hurt", TriggerSignal{ WhenSource{ At("wound"), At("wound") }, 0.8f, 3 }, CurveRef{ "@flash" } });
		r.signals.push_back(Signal{ "drop", PayloadSignal{ At("hurt"), PayloadField::kValue }, std::nullopt });
		r.signals.push_back(Signal{ "where", PayloadSignal{ At("hit"), PayloadField::kPosition }, std::nullopt });
		r.signals.push_back(Signal{ "hits", CounterSignal{ At("hit"), At("surge"), Param{ 5.0f } }, std::nullopt });
		r.signals.push_back(Signal{ "heat", AccumulateSignal{ At("hit"), 0.5f }, std::nullopt });
		r.signals.push_back(Signal{ "flicker", NoiseSignal{ 3.0f, 0.2f, 7 }, std::nullopt });
		r.signals.push_back(Signal{ "eased", SmoothSignal{ At("health"), 0.4f }, std::nullopt });
		r.signals.push_back(Signal{ "palette", GradientSignal{ At("eased"), { { 0.0f, std::array<Param, 3>{ 0.0f, 0.0f, 1.0f } }, { 1.0f, Vec3Param{ At("red") } } } }, std::nullopt });
		r.signals.push_back(Expr("level", "clamp(@pulse * 2 + @health / @healthMax, 0, 1)"));
		r.signals.push_back(Expr("mix", "lerp(@red, @palette, @level)"));
		r.signals.push_back(Expr("shaped", "@flash(@level) * 2"));

		r.curves.push_back({ "rest", "x / 0.05" });
		r.curves.push_back({ "flash", "pow(1 - x, 2)" });
		r.curves.push_back({ "crisp", "(x - mean) * 3 + 0.5" });

		ImageSource fill;
		fill.path = "Effects\\DarkSwirls.dds";
		fill.channel = ImageChannel::kLuma;
		fill.scroll = Vec2Param{ At("scroll") };
		fill.tile = Vec2Param{ std::array<Param, 2>{ 3.0f, At("one") } };
		fill.mirror = { false, true };
		fill.transpose = true;
		fill.mip = 2.0f;
		r.sources.push_back({ "fill", fill });
		ImageSource painted;
		painted.path = "WornEnchantmentPBR\\iron_mask.dds";
		painted.channel = ImageChannel::kR;
		painted.space = ImageSpace::kMesh;
		r.sources.push_back({ "painted", painted });
		ImageSource colourField;
		colourField.path = "Effects\\Fire.dds";
		colourField.scroll = Vec2Param{ std::array<Param, 2>{ 0.0f, 0.1f } };
		r.sources.push_back({ "flames", colourField });
		r.sources.push_back({ "metallic", MaterialSource{ MaterialChannel::kMetallic } });
		r.sources.push_back({ "roughness", MaterialSource{ MaterialChannel::kRoughness } });
		r.sources.push_back({ "albedo", MaterialSource{ MaterialChannel::kDiffuseRgb } });
		r.sources.push_back({ "pos", BakeSource{ PositionBake{} } });
		r.sources.push_back({ "up", BakeSource{ WorldUpBake{} } });
		r.sources.push_back({ "body", BakeSource{ PartitionBake{ 32 } } });
		r.sources.push_back({ "circlet", BakeSource{ PartitionBake{ 42 } } });
		r.sources.push_back({ "hands", BakeSource{ BoneWeightBake{ { "NPC L Hand [LHnd]", "NPC R Hand [RHnd]" } } } });
		r.sources.push_back({ "u", UvSource{ UvAxis::kU } });
		r.sources.push_back({ "fromHand", DistanceSource{ std::string{ "NPC R Hand [RHnd]" } } });
		r.sources.push_back({ "fromPoint", DistanceSource{ Vec3{ 0.0f, 0.0f, 100.0f } } });
		r.sources.push_back({ "ring", RippleSource{ At("hit"), 120.0f, At("one"), 0.8f, RippleShape::kDisc } });
		r.sources.push_back({ "pieces", BakeSource{ ComponentIdBake{} } });
		r.sources.push_back({ "charts", BakeSource{ ChartIdBake{} } });
		MaterialClustersSource tuned;
		tuned.clusters = 3;
		tuned.luma = 2.0f;
		tuned.seed = 7;
		r.sources.push_back({ "materials", tuned });

		r.masks.push_back({ "metal", "@metallic" });
		r.masks.push_back({ "polished", "@metallic * smoothstep(0.55, 0.65, 1 - @roughness)" });
		r.masks.push_back({ "trimmed", "max(@polished, @painted) * @crisp(@up)" });
		r.masks.push_back({ "live", "@metallic * (@level > 0.5)" });

		const auto layer = [](LayerSource a_source, Blend a_blend, Param a_opacity) {
			Layer l;
			l.source = std::move(a_source);
			l.blend = a_blend;
			l.opacity = std::move(a_opacity);
			return l;
		};
		{
			MaterialOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kEmissive;
			o.scalars.strength = At("level");
			o.selector.anyOf = { SelectorTerm{ SelectorKind::kTexture, {}, "*iron*" }, SelectorTerm{ SelectorKind::kGeometry, {}, "Armor*" }, SelectorTerm{ SelectorKind::kAddon, Form("0x12E48~Skyrim.esm"), {} } };
			o.replace = true;
			auto l = layer(At("fill"), Blend::kReplace, 1.0f);
			l.color = Vec3Param{ At("mix") };
			l.mask = At("polished");
			l.curve = CurveRef{ "@crisp" };
			o.stack.push_back(l);
			o.stack.push_back(layer(Vec3{ 0.5f, 0.5f, 0.5f }, Blend::kScreen, At("pulse")));
			auto masked = layer(At("trimmed"), Blend::kAdd, 0.5f);
			masked.color = Vec3Param{ std::array<Param, 3>{ 1.0f, At("one"), 0.0f } };
			o.stack.push_back(masked);
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kDiffuse;
			auto l = layer(At("painted"), Blend::kLerp, At("level"));
			l.channels = *ChannelSet::Parse("a");
			o.stack.push_back(l);
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kRmaos;
			auto l = layer(At("fill"), Blend::kSubtract, 1.0f);
			l.channels = *ChannelSet::Parse("r");
			o.stack.push_back(l);
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kNormal;
			o.stack.push_back(layer(At("flames"), Blend::kNormal, 1.0f));
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kHeight;
			o.scalars.scale = At("pulse");
			o.stack.push_back(layer(At("ring"), Blend::kAdd, 1.0f));
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kShell;
			o.slot = Slot::kFuzz;
			o.scalars.color = Vec3Param{ At("red") };
			o.scalars.weight = At("level");
			o.stack.push_back(layer(At("fill"), Blend::kMultiply, 1.0f));
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kGlint;
			o.scalars.screenSpaceScale = 1.5f;
			o.scalars.logMicrofacetDensity = At("one");
			o.scalars.microfacetRoughness = 0.02f;
			o.scalars.densityRandomization = 2.0f;
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kCoat;
			o.scalars.roughness = At("pulse");
			o.scalars.level = At("one");
			r.outputs.push_back(o);
		}
		{
			MaterialOutput o;
			o.surface = Surface::kMaterial;
			o.slot = Slot::kSubsurface;
			o.scalars.color = Vec3Param{ At("red") };
			o.scalars.thickness = At("level");
			r.outputs.push_back(o);
		}
		{
			LightOutput l;
			l.bones = SkinnedBones{ 3, 0.1f };
			l.offset = Vec3Param{ std::array<Param, 3>{ 0.0f, 0.0f, At("one") } };
			l.color = Vec3Param{ At("mix") };
			l.intensity = At("level");
			l.size = 0.3f;
			l.cutoff = At("one");
			l.shadow = true;
			l.bulb = Form("MagicLightWhite01");
			l.replace = true;
			r.outputs.push_back(l);
		}
		{
			LightOutput l;
			l.bones = NamedBones{ { "NPC Head [Head]" } };
			l.color = Vec3Param{ std::array<Param, 3>{ 1.0f, 0.5f, 0.25f } };
			l.intensity = 0.5f;
			r.outputs.push_back(l);
		}

		r.shell.material = ShellMaterial::kPbrCopy;
		r.shell.blend = ShellBlend::kAlpha;
		r.shell.depthBias = false;
		r.shell.alphaTest = 0.3f;
		r.shell.alpha = At("level");
		r.shell.rimPower = 4.0f;
		r.shell.emissive = At("pulse");
		r.shell.pose.inflate = std::array<Param, 3>{ 0.01f, At("pulse"), At("pulse") };
		r.shell.pose.offset = Vec3Param{ At("where") };
		r.shell.pose.scale = At("level");
		r.shell.pose.scalePoint = Vec3{ 0.0f, 0.0f, 100.0f };
		r.shell.pose.spin = At("ramp");
		r.shell.pose.spinAxis = Vec3{ 0.0f, 1.0f, 0.0f };

		Variant iron;
		iron.name = "iron";
		iron.key = Form("0x12E49~Skyrim.esm");
		iron.overrides = { { "one", Value{ 0.5f } }, { "red", Value{ Vec3{ 0, 1, 0 } } } };
		r.variants.push_back(iron);
		Variant body;
		body.name = "body only";
		body.key = Selector{ { SelectorTerm{ SelectorKind::kGeometry, {}, "*Body*" } } };
		body.overrides = { { "pulse", Value{ 0.0f } } };
		r.variants.push_back(body);
		return r;
	}

	void Forms()
	{
		const auto k = FormKey::Parse("0x92DED~Skyrim.esm");
		Check(k && k->file == "Skyrim.esm" && k->localId == 0x92ded, "form key parses");
		Check(k->ToString() == "0x92DED~Skyrim.esm", "form key formats");
		Check(*k == Key("0x92ded~skyrim.ESM"), "form keys compare case-insensitively");
		Check(!(*k == Key("0x92DEE~Skyrim.esm")), "different ids differ");
		Check(!FormKey::Parse("skyrim~092ded") && !FormKey::Parse("0x~Skyrim.esm") && !FormKey::Parse("0x92DED~") && !FormKey::Parse("EnchArmorMagickaFXS"), "non form keys rejected");
		Check(FormKey::Parse("0x800~My Mod.esp")->file == "My Mod.esp", "plugin names may hold spaces");
		const auto edid = Form("EnchArmorMagickaFXS");
		Check(!edid.Resolved() && edid.text == "EnchArmorMagickaFXS", "an editor ID is unresolved until the store resolves it");
		Check(Form("0x92DED~Skyrim.esm").Resolved(), "a form key resolves on parse");
		Check(CurveRef{ "@rest" }.Named() == "rest" && !CurveRef{ "x / 0.05" }.Named() && !CurveRef{ "@rest(x)" }.Named(), "curve reference naming");
		Check(RecipeKey{ KeyKind::kEffectShader, Form("EnchArmorMagickaFXS"), {} }.ToString() == "effectShader:EnchArmorMagickaFXS", "key text for logs");
	}

	void Globs()
	{
		Check(GlobMatch("*", "anything"), "star matches all");
		Check(GlobMatch("armor/iron/*", "Armor\\Iron\\cuirass_d.dds"), "case-insensitive and slash-agnostic");
		Check(GlobMatch("*iron*", "textures/armor/IRON/x.dds"), "inner star");
		Check(!GlobMatch("*iron", "iron/x.dds"), "anchored at the end");
		Check(!GlobMatch("a?c", "abc") && GlobMatch("a?c", "a?c"), "? is a literal");
		Check(GlobMatch("", "") && !GlobMatch("", "x"), "empty glob matches only empty");
		Check(GlobMatch("anim.Foot*", "anim.FootLeft") && !GlobMatch("anim.Foot*", "anim.JumpUp"), "event id globs");

		GeometryIdentity g{ Key("0x12E48~Skyrim.esm"), "ArmorBody", "textures\\armor\\iron\\cuirass_d.dds" };
		Check(Matches(Selector{}, g), "empty selector matches all");
		Check(Matches(Selector{ { SelectorTerm{ SelectorKind::kAddon, Form("0x12E48~Skyrim.esm"), {} } } }, g), "addon selector");
		Check(!Matches(Selector{ { SelectorTerm{ SelectorKind::kAddon, Form("SomeAddon"), {} } } }, g), "an unresolved addon never matches");
		Check(Matches(Selector{ { SelectorTerm{ SelectorKind::kGeometry, {}, "armor*" } } }, g), "geometry selector");
		Check(Matches(Selector{ { SelectorTerm{ SelectorKind::kTexture, {}, "*steel*" }, SelectorTerm{ SelectorKind::kGeometry, {}, "ArmorBody" } } }, g), "any-of");

		Check(ChannelSet::Parse("rgba") == ChannelSet{} && ChannelSet::Parse("ga") == ChannelSet{ false, true, false, true } && !ChannelSet::Parse("") && !ChannelSet::Parse("x") && !ChannelSet::Parse("rgbaa"), "channel sets");
		Check(BipedSlotFromName("body") == 32u && BipedSlotFromName("Feet") == 37u && !BipedSlotFromName("torso"), "biped slot names");
		Check(BipedSlotName(33) == "hands" && !BipedSlotName(42), "biped slot numbers");
	}

	void CanonicalFile()
	{
		const auto path = std::filesystem::path{ WEPBR_FIXTURES_DIR }.parent_path().parent_path() / "schema" / "example-magicka.json";
		const auto text = test::ReadFile(path);
		Check(!text.empty(), "schema/example-magicka.json is readable at " + path.string());
		const auto loaded = ParseRecipe(text, "example-magicka");
		Check(loaded.recipe.has_value(), "the canonical file parses");
		Check(NoErrors(loaded.diagnostics), "the canonical file has no errors: " + Errors(loaded.diagnostics));
		if (!loaded.recipe) {
			return;
		}
		const auto& r = *loaded.recipe;
		Check(r.metadata.name == "Magicka (vanilla)" && r.metadata.imported == "WornEnchantmentPBR 0.1.0", "metadata read");
		Check(r.keys.size() == 1 && r.keys[0].kind == KeyKind::kEffectShader && r.keys[0].form.text == "EnchArmorMagickaFXS", "key read");
		Check(r.signals.size() == 20 && r.curves.size() == 5 && r.sources.size() == 8 && r.masks.size() == 1 && r.outputs.size() == 5 && r.variants.size() == 1, "row counts");
		const auto* step = r.FindSignal("step");
		const auto* trig = step ? Get<TriggerSignal>(step->kind) : nullptr;
		const auto* ev = trig ? Get<EventSource>(trig->source) : nullptr;
		Check(ev && ev->event == "anim.FootLeft" && ev->at == "NPC L Foot [Lft ]" && trig->max == 2, "trigger with at read");
		const auto again = ParseRecipe(SerializeRecipe(r), "example-magicka");
		Check(again.recipe && *again.recipe == r, "serialise then parse is identical");
		Check(again.recipe && SerializeRecipe(*again.recipe) == SerializeRecipe(r), "second serialisation is byte-identical");
		Check(IsAnimated(r, *Get<MaterialOutput>(r.outputs[0]) == *Get<MaterialOutput>(r.outputs[0]) ? r.outputs[0] : r.outputs[0]) && IsAnimated(r, r.outputs[2]), "scrolled stacks are animated");
	}

	void RoundTrip()
	{
		const auto recipe = Everything();
		const auto diags = Validate(recipe);
		Check(NoErrors(diags), "the full recipe validates: " + Errors(diags));
		const auto text = SerializeRecipe(recipe);
		const auto loaded = ParseRecipe(text, "everything");
		Check(loaded.recipe.has_value(), "serialised recipe parses");
		Check(NoErrors(loaded.diagnostics), "parsed recipe has no errors: " + Errors(loaded.diagnostics));
		if (loaded.recipe) {
			const auto& got = *loaded.recipe;
			Check(got == recipe, "round trip is identical");
			if (!(got == recipe)) {
				Check(got.metadata == recipe.metadata, "  metadata equal");
				Check(got.keys == recipe.keys, "  keys equal");
				Check(got.signals == recipe.signals, "  signals equal");
				Check(got.curves == recipe.curves, "  curves equal");
				Check(got.sources == recipe.sources, "  sources equal");
				Check(got.masks == recipe.masks, "  masks equal");
				Check(got.outputs == recipe.outputs, "  outputs equal");
				Check(got.shell == recipe.shell, "  shell equal");
				Check(got.variants == recipe.variants, "  variants equal");
				for (std::size_t i = 0; i < std::min(got.signals.size(), recipe.signals.size()); ++i) {
					if (!(got.signals[i] == recipe.signals[i])) {
						std::printf("    signal %s differs\n", recipe.signals[i].name.c_str());
					}
				}
			}
			Check(SerializeRecipe(got) == text, "second serialisation is byte-identical");
		}
		Check(text.find("0.05000000") == std::string::npos, "floats serialise as their shortest form");
		Check(text.find("\"meta\": {") != std::string::npos && text.find("kept verbatim") != std::string::npos, "meta survives");
		Check(text.find("\"partition\": \"body\"") != std::string::npos && text.find("\"partition\": 42") != std::string::npos, "partitions write names when they have one");
		Check(text.find("\"kind\"") == std::string::npos && text.find("\"name\": \"one\"") == std::string::npos, "no kind or name fields");

		Recipe minimal;
		minimal.keys = { RecipeKey{ KeyKind::kDefault, {}, {} } };
		const auto minimalText = SerializeRecipe(minimal);
		Check(minimalText == "{\n  \"format\": 1,\n  \"keys\": [\n    \"default\"\n  ]\n}\n", "an empty recipe writes only format and keys:\n" + minimalText);
		const auto minimalLoaded = ParseRecipe(minimalText, "");
		Check(minimalLoaded.recipe && *minimalLoaded.recipe == minimal && NoErrors(minimalLoaded.diagnostics), "an empty recipe round trips");
	}

	// The kinds the mesh and material analyses feed: the two id-map bakes
	// and the cluster map with its settings.
	void AnalysisKinds()
	{
		const auto text = R"json({"format": 1, "keys": ["default"], "sources": {
			"pieces": {"bake": "componentId"},
			"charts": {"bake": "chartId"},
			"plain": {"materialClusters": {}},
			"tuned": {"materialClusters": {"clusters": 3, "weights": {"luma": 2, "occlusion": 0}, "seed": 7, "iterations": 64}}
		}})json";
		auto r = ParseRecipe(text, "x");
		Check(r.recipe && NoErrors(r.diagnostics), "the analysis kinds parse: " + Errors(r.diagnostics));
		if (!r.recipe) {
			return;
		}
		const auto* pieces = r.recipe->FindSource("pieces");
		const auto* charts = r.recipe->FindSource("charts");
		Check(pieces && Get<BakeSource>(pieces->kind) && Is<ComponentIdBake>(Get<BakeSource>(pieces->kind)->bake), "componentId reads as its bake");
		Check(charts && Get<BakeSource>(charts->kind) && Is<ChartIdBake>(Get<BakeSource>(charts->kind)->bake), "chartId reads as its bake");
		const auto* plain = r.recipe->FindSource("plain");
		const auto* plainKind = plain ? Get<MaterialClustersSource>(plain->kind) : nullptr;
		Check(plainKind && *plainKind == MaterialClustersSource{}, "an empty materialClusters is the defaults");
		const auto* tuned = r.recipe->FindSource("tuned");
		const auto* tunedKind = tuned ? Get<MaterialClustersSource>(tuned->kind) : nullptr;
		Check(tunedKind && tunedKind->clusters == 3 && Near(tunedKind->luma, 2.0f) && Near(tunedKind->occlusion, 0.0f) && Near(tunedKind->roughness, 1.0f) && tunedKind->seed == 7 && tunedKind->iterations == 64, "materialClusters reads its settings, the rest at their defaults");
		Check(NoErrors(Validate(*r.recipe)), "the analysis kinds validate: " + Errors(Validate(*r.recipe)));
		const auto written = SerializeRecipe(*r.recipe);
		Check(written.find("\"bake\": \"componentId\"") != std::string::npos && written.find("\"bake\": \"chartId\"") != std::string::npos, "the id-map bakes write as their names");
		Check(written.find("\"materialClusters\": {}") != std::string::npos, "a default materialClusters writes an empty object");
		Check(written.find("\"roughness\"") == std::string::npos && written.find("\"luma\": 2") != std::string::npos && written.find("\"occlusion\": 0") != std::string::npos, "only the non-default weights are written");
		const auto again = ParseRecipe(written, "x");
		Check(again.recipe && *again.recipe == *r.recipe && SerializeRecipe(*again.recipe) == written, "the analysis kinds round trip byte-identical");
		if (tunedKind) {
			Check(SourceType(*tuned) == ValueType::kScalar, "a cluster map is a scalar");
			const auto described = DescribeSource(tuned->kind);
			Check(described.starts_with("materialClusters, 3 clusters") && described.find("luma 2") != std::string::npos && described.find("occlusion 0") != std::string::npos && described.find("roughness") == std::string::npos, "the description names the count and the non-default weights: " + described);
		}
		Check(DescribeSource(MaterialClustersSource{}) == "materialClusters, 4 clusters", "the default description is the count alone");
		Check(SourceKindName(MaterialClustersSource{}) == "materialClusters" && BakeKindName(ComponentIdBake{}) == "componentId" && BakeKindName(ChartIdBake{}) == "chartId", "the kinds' words");
		const auto defaultKind = DefaultSourceKind("materialClusters");
		const auto defaultBake = DefaultBakeKind("chartId");
		Check(defaultKind && Is<MaterialClustersSource>(*defaultKind) && defaultBake && Is<ChartIdBake>(*defaultBake), "the defaults by word");

		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"clusters": 9}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'clusters' is 1..8") && r.recipe && !r.recipe->FindSource("m"), "clusters past the cap are refused and the row dropped");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"iterations": 0}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'iterations' is 1..256"), "zero iterations refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"weights": {"luma": 11}}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'weights.luma' is 0..10"), "a weight past the cap refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"weights": [1, 1]}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'weights' is an object"), "weights as an array refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"seed": -1}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'seed' is a whole number"), "a negative seed refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": {"weights": {"lumen": 1}}}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "unknown key 'lumen'"), "an unknown weight reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"materialClusters": 4}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'materialClusters' takes an object"), "a bare number refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"m": {"bake": "componentIds"}}})json", "x");
		Check(HasError(r.diagnostics, "source m", "'bake' is"), "a misspelt bake name refused");

		Recipe                 built;
		built.keys = { RecipeKey{ KeyKind::kDefault, {}, {} } };
		MaterialClustersSource bad;
		bad.clusters = 0;
		bad.iterations = 300;
		bad.luma = -1.0f;
		built.sources.push_back({ "m", bad });
		const auto diags = Validate(built);
		Check(HasError(diags, "source m", "'clusters' is 1..8") && HasError(diags, "source m", "'iterations' is 1..256") && HasError(diags, "source m", "'weights.luma' is 0..10"), "validation bounds the settings of a built record: " + Errors(diags));
	}

	void Reading()
	{
		Check(!ParseRecipe("not json", "x").recipe, "non-JSON yields no recipe");
		Check(!ParseRecipe("[1,2]", "x").recipe, "a JSON array yields no recipe");

		auto r = ParseRecipe(R"json({
			// a comment
			"format": 1, /* another */
			"keys": ["default"],
			"signals": { "one": { "constant": 1 } }
		})json", "x");
		Check(r.recipe && NoErrors(r.diagnostics), "comments are accepted: " + Errors(r.diagnostics));

		r = ParseRecipe(R"json({"keys": ["default"]})json", "x");
		Check(HasError(r.diagnostics, "recipe", "'format' is required"), "missing format reported");
		r = ParseRecipe(R"json({"format": 2, "keys": ["default"]})json", "x");
		Check(!r.recipe && HasError(r.diagnostics, "recipe", "newer than this loader"), "a newer format is refused");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "bogus": 1})json", "x");
		Check(HasError(r.diagnostics, "recipe", "unknown key 'bogus'"), "unknown top-level key reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"a": {"constant": 1}, "a": {"constant": 2}}})json", "x");
		Check(HasError(r.diagnostics, "file", "duplicate key 'a'"), "duplicate JSON keys reported: " + Errors(r.diagnostics));
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"x": {"constant": 1, "expr": "1"}}})json", "x");
		Check(HasError(r.diagnostics, "signal x", "two kind keys"), "two kinds in a row reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"x": {"wibble": 1}}})json", "x");
		Check(HasError(r.diagnostics, "signal x", "unknown signal kind 'wibble'"), "unknown kind reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"x": {"pulse": {"base": 0, "amplitud": 1}}}})json", "x");
		Check(HasError(r.diagnostics, "signal x", "unknown key 'amplitud'"), "typo inside a kind object reported");
		r = ParseRecipe(R"json({"format": 1, "keys": [{"weapon": "X"}]})json", "x");
		Check(HasError(r.diagnostics, "recipe", "unknown key kind 'weapon'"), "unknown key kind reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"f": {"image": {"path": "a.dds"}}},
			"outputs": [{"target": "shell", "slot": "emissive", "strength": "@one", "stack": [{"source": "@f", "opacity": "fillLevel"}]}]})json", "x");
		Check(HasError(r.diagnostics, "output 0 layer 0", "must start with '@'"), "bare reference string reported on the layer row: " + Errors(r.diagnostics));
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"c": {"constant": [255, 128, 0]}}, "outputs": [{"target": "shell", "slot": "fuzz", "color": [255, 0, 0], "weight": 1, "stack": []}]})json", "x");
		const auto* c = r.recipe ? r.recipe->FindSignal("c") : nullptr;
		const auto* cv = c ? Get<ConstantSignal>(c->kind) : nullptr;
		Check(cv && Get<Vec3>(cv->value) && Near(Get<Vec3>(cv->value)->y, 128.0f / 255.0f), "0..255 colours read as 0..1");
		const auto* fuzz = r.recipe && !r.recipe->outputs.empty() ? Get<MaterialOutput>(r.recipe->outputs[0]) : nullptr;
		const auto* fc = fuzz && fuzz->scalars.color ? Get<std::array<Param, 3>>(*fuzz->scalars.color) : nullptr;
		Check(fc && Near(std::get<float>((*fc)[0]), 1.0f) && Near(std::get<float>((*fc)[1]), 0.0f), "0..255 colour parameters read as 0..1");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"p": {"bake": {"partition": "torso"}}}})json", "x");
		Check(HasError(r.diagnostics, "source p", "unknown biped slot name 'torso'"), "unknown partition name reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"t": {"trigger": {"event": "a", "plugin": "b", "lifetime": 1, "max": 1}}}})json", "x");
		Check(HasError(r.diagnostics, "signal t", "two kind keys"), "a trigger with two sources reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"t": {"trigger": {"when": "@x", "value": "@x", "lifetime": 1, "max": 0}}}})json", "x");
		Check(HasError(r.diagnostics, "signal t", "'max' must be at least 1"), "max below 1 reported");
		r = ParseRecipe(R"json({"format": 1, "keys": ["default"], "meta": {"anything": [1, {"deep": true}]}})json", "x");
		Check(r.recipe && r.recipe->metadata.meta.find("deep") != std::string::npos, "meta kept");
		Check(r.recipe && SerializeRecipe(*r.recipe).find("\"deep\": true") != std::string::npos, "meta written back");
	}

	void Validation()
	{
		auto r = ParseRecipe(R"json({"format": 1, "keys": ["default"],
			"signals": {
				"one": {"constant": 1}, "red": {"constant": [1, 0, 0]},
				"a": {"expr": "@b * 2"}, "b": {"expr": "@a + 1"},
				"orphan": {"pulse": {"period": "@nothing"}},
				"vecPeriod": {"pulse": {"period": "@red"}},
				"badCurve": {"constant": 1, "curve": "@missing"},
				"inlineBad": {"constant": 1, "curve": "x +"},
				"colourExpr": {"expr": "@red * 2"},
				"mix": {"expr": "@red + [1, 1]"},
				"notTrigger": {"payload": {"trigger": "@one", "field": "value"}}
			},
			"curves": {"broken": "x + @one"},
			"sources": {"fill": {"image": {"path": "x.dds", "scroll": "@red"}}, "metallic": {"material": "metallic"}, "ring": {"ripple": {"trigger": "@one"}}},
			"masks": {"self": "@self * 2", "typed": "@fill + [1, 1]", "hasX": "x", "curveless": "@unknownCurve(@metallic)", "ok": "@metallic * @colourExpr"},
			"outputs": [
				{"target": "material", "slot": "emissive", "strength": "@red", "stack": [{"source": "@fill", "blend": "normal", "opacity": "@red", "mask": "@nope"}]},
				{"target": "shell", "slot": "fuzz", "color": "@red", "weight": "@one", "stack": []},
				{"target": "shell", "slot": "glint", "stack": []},
				{"target": "material", "slot": "coat", "roughness": "@one", "level": "@one", "stack": []},
				{"target": "material", "slot": "subsurface", "color": "@red", "thickness": "@one", "stack": []},
				{"target": "light", "bones": {"skinned": {"max": 2}}, "color": "@one", "intensity": "@red"},
				{"target": "shell", "slot": "emissive", "strength": "@one", "stack": []}
			],
			"shell": {"material": "vanilla"},
			"variants": [{"name": "v", "key": {"armor": "0x1~Skyrim.esm"}, "overrides": {"one": [1, 1, 1], "two": 3}}]
		})json", "x");
		Check(r.recipe.has_value(), "recipe with row errors still loads");
		const auto& d = r.diagnostics;
		Check(HasError(d, "signal a", "cycle: a -> b -> a") || HasError(d, "signal b", "cycle"), "cycle reported on its row: " + Errors(d));
		Check(HasError(d, "signal orphan", "unknown signal '@nothing'"), "unknown reference on its row");
		Check(HasError(d, "signal vecPeriod", "'period' must be a scalar"), "vector where a scalar is needed");
		Check(HasError(d, "signal badCurve", "unknown curve '@missing'"), "unknown named curve");
		Check(HasError(d, "signal inlineBad", "curve:"), "bad inline curve");
		Check(!HasError(d, "signal colourExpr", ""), "a colour expression is fine");
		Check(HasError(d, "signal mix", "mixes vec3 with vec2"), "vec3 with vec2 in an expression");
		Check(HasError(d, "signal notTrigger", "must name a trigger"), "payload of a non-trigger");
		Check(HasError(d, "curve broken", "reads no rows"), "a curve reading a row");
		Check(HasError(d, "source fill", "'scroll' must be a vec2"), "scroll by a vec3 signal");
		Check(HasError(d, "source ring", "must name a trigger"), "ripple of a non-trigger");
		Check(HasError(d, "mask self", "reads itself"), "mask self-reference");
		Check(HasError(d, "mask typed", "mixes vec3 with vec2"), "mask type error");
		Check(HasError(d, "mask hasX", "only defined inside a curve"), "x outside a curve");
		Check(HasError(d, "mask curveless", "unknown curve '@unknownCurve'"), "mask calling an unknown curve");
		Check(!HasError(d, "mask ok", ""), "a mask reading a source and a signal is fine");
		Check(HasError(d, "output 0", "'strength' must be a scalar"), "vector strength");
		Check(HasError(d, "output 0 layer 0", "'opacity' must be a scalar"), "vector opacity");
		Check(HasError(d, "output 0 layer 0", "unknown mask '@nope'"), "unknown mask");
		Check(HasError(d, "output 0 layer 0", "'normal' is valid only"), "normal blend outside the normal stack");
		Check(HasError(d, "output 1", "vanilla shell has only the emissive slot"), "fuzz on a vanilla shell");
		Check(HasError(d, "output 2", "vanilla shell has only the emissive slot"), "glint on a vanilla shell");
		Check(std::ranges::any_of(d, [](const Diagnostic& x) { return x.where == "output 2" && x.message.find("exclude each other") != std::string::npos; }), "fuzz and glint exclude each other");
		Check(std::ranges::any_of(d, [](const Diagnostic& x) { return x.where == "output 4" && x.message.find("exclude each other") != std::string::npos; }), "coat and subsurface exclude each other");
		Check(HasError(d, "output 5", "'color' must be a vec3") && HasError(d, "output 5", "'intensity' must be a scalar"), "light colour and intensity types");
		Check(!HasError(d, "output 6", "vanilla shell"), "emissive is the one slot a vanilla shell has");
		Check(HasError(d, "variant v", "override of 'one' is a vec3") && HasError(d, "variant v", "unknown signal 'two'"), "variant override checks");

		auto scalars = ParseRecipe(R"json({"format": 1, "keys": ["default"],
			"signals": {"one": {"constant": 1}, "red": {"constant": [1, 0, 0]}},
			"outputs": [
				{"target": "shell", "slot": "diffuse", "stack": []},
				{"target": "material", "slot": "coat", "roughness": "@one", "stack": []},
				{"target": "material", "slot": "emissive", "stack": []},
				{"target": "material", "slot": "height", "scale": "@red", "stack": []},
				{"target": "material", "slot": "glint", "screenSpaceScale": "@red", "stack": []},
				{"target": "material", "slot": "subsurface", "stack": []},
				{"target": "material", "slot": "coat", "roughness": "@one", "level": 0.5, "stack": []},
				{"target": "material", "slot": "normal", "stack": [{"source": [0.5, 0.5, 1], "blend": "normal", "opacity": 1}]}
			],
			"shell": {"material": "vanilla"}
		})json", "x");
		const auto& sd = scalars.diagnostics;
		Check(HasError(sd, "output 0", "vanilla shell has only the emissive slot"), "diffuse on a vanilla shell");
		Check(HasError(sd, "output 1", "slot 'coat' needs 'level'") && !HasError(sd, "output 1", "needs 'roughness'"), "a missing required scalar is named");
		Check(HasError(sd, "output 2", "slot 'emissive' needs 'strength'"), "emissive needs strength");
		Check(HasError(sd, "output 3", "'scale' must be a scalar"), "a vector where the scalar goes");
		Check(HasError(sd, "output 4", "'screenSpaceScale' must be a scalar") && !HasError(sd, "output 4", "needs"), "glint parameters are optional but typed");
		Check(HasError(sd, "output 5", "slot 'subsurface' needs 'color'") && HasError(sd, "output 5", "slot 'subsurface' needs 'thickness'"), "subsurface needs colour and thickness");
		Check(std::ranges::any_of(sd, [](const Diagnostic& x) { return x.where == "output 5" && x.severity == Severity::kWarning && x.message.find("'coat' and 'subsurface'") != std::string::npos; }), "coat then subsurface on the material");
		Check(std::ranges::none_of(sd, [](const Diagnostic& x) { return x.where == "output 6" && x.message.find("'coat' and 'coat'") != std::string::npos; }), "a second coat output is not excluded by the first");
		Check(std::ranges::any_of(sd, [](const Diagnostic& x) { return x.where == "output 6" && x.message.find("'subsurface' and 'coat'") != std::string::npos; }), "subsurface then coat on the material");
		Check(!HasError(sd, "output 7", ""), "the normal blend on the normal stack");

		auto dup = ParseRecipe(R"json({"format": 1, "keys": ["default"], "sources": {"metal": {"material": "metallic"}}, "masks": {"metal": "1"}})json", "x");
		Check(HasError(dup.diagnostics, "mask metal", "a source has the same name"), "mask and source sharing a name");
		auto badName = ParseRecipe(R"json({"format": 1, "keys": ["default"], "signals": {"9lives": {"constant": 1}}})json", "x");
		Check(HasError(badName.diagnostics, "signal '9lives'", "names are letters"), "bad row name");
	}

	// The format's words: one table each, read by the name and parse functions.
	void Words()
	{
		Check(SurfaceName(Surface::kMaterial) == "material" && SurfaceName(Surface::kShell) == "shell", "surface words");
		Check(ParseSurface("shell") == Surface::kShell && !ParseSurface("light") && !ParseSurface(""), "a surface parses from its word alone");
		Check(Choices(kKeyKinds) == "default, material, keyword, armor, effectShader, enchantment, magicEffect", "key kinds in enum order, default first");
		Check(WordsOf(kShellMaterials) == std::vector<std::string>{ "pbrCopy", "vanilla" } && WordsOf(kShellBlends) == std::vector<std::string>{ "additive", "alpha" }, "shell choice lists come from the tables");
		Check(KeyKindName(KeyKind::kMagicEffect) == "magicEffect" && KeyKindName(static_cast<KeyKind>(99)) == "?", "a key kind names, an unknown value names '?'");
		Check(SlotName(static_cast<Slot>(99)) == "?" && ScalarFieldName(static_cast<ScalarField>(99)) == "?", "unknown slots and fields name '?', never index past a table");
		for (const auto& row : kSlots) {
			Check(ParseScalarField(row.name) == std::nullopt || row.value == Slot::kRmaos, std::format("no slot word is a scalar field word but '{}'", row.name));
		}
		const auto r = ParseRecipe(R"json({"format": 1, "keys": [{"default": "X"}]})json", "x");
		Check(HasError(r.diagnostics, "recipe", "unknown key kind 'default'"), "a default key written as an object is refused");
	}

	// The format's slot rules, which Validate and the menu's board share.
	void SlotRules()
	{
		constexpr Slot every[]{ Slot::kDiffuse, Slot::kEmissive, Slot::kRmaos, Slot::kNormal, Slot::kHeight, Slot::kFuzz, Slot::kGlint, Slot::kCoat, Slot::kSubsurface };
		static_assert(std::size(every) == kSlotCount);
		constexpr ScalarField fields[]{ ScalarField::kStrength, ScalarField::kScale, ScalarField::kColor, ScalarField::kWeight, ScalarField::kScreenSpaceScale, ScalarField::kLogMicrofacetDensity, ScalarField::kMicrofacetRoughness, ScalarField::kDensityRandomization, ScalarField::kRoughness, ScalarField::kLevel, ScalarField::kThickness };
		static_assert(std::size(fields) == kScalarFieldCount);
		constexpr Blend blends[]{ Blend::kReplace, Blend::kMultiply, Blend::kAdd, Blend::kSubtract, Blend::kScreen, Blend::kLerp, Blend::kNormal };

		// SlotsOf and SurfaceHasSlot
		for (const auto shell : { ShellMaterial::kPbrCopy, ShellMaterial::kVanilla }) {
			Check(std::ranges::equal(SlotsOf(Surface::kMaterial, shell), every), "a material offers every slot in enum order whatever the shell is");
		}
		Check(std::ranges::equal(SlotsOf(Surface::kShell, ShellMaterial::kPbrCopy), every), "a PBR-copy shell offers every slot");
		const auto vanilla = SlotsOf(Surface::kShell, ShellMaterial::kVanilla);
		Check(vanilla.size() == 1 && vanilla[0] == Slot::kEmissive, "a vanilla shell offers emissive only");
		for (const auto slot : every) {
			Check(SurfaceHasSlot(Surface::kMaterial, ShellMaterial::kVanilla, slot) && SurfaceHasSlot(Surface::kShell, ShellMaterial::kPbrCopy, slot), std::format("'{}' on a material and on a PBR-copy shell", SlotName(slot)));
			Check(SurfaceHasSlot(Surface::kShell, ShellMaterial::kVanilla, slot) == (slot == Slot::kEmissive), std::format("'{}' on a vanilla shell", SlotName(slot)));
		}

		// ScalarsOf and ScalarRequired
		struct SlotScalarRule
		{
			Slot                     slot;
			std::vector<ScalarField> carried;
			bool                     required;  // every carried field, or none (glint)
		};
		const SlotScalarRule rules[]{
			{ Slot::kDiffuse, {}, false },
			{ Slot::kRmaos, {}, false },
			{ Slot::kNormal, {}, false },
			{ Slot::kEmissive, { ScalarField::kStrength }, true },
			{ Slot::kHeight, { ScalarField::kScale }, true },
			{ Slot::kFuzz, { ScalarField::kColor, ScalarField::kWeight }, true },
			{ Slot::kGlint, { ScalarField::kScreenSpaceScale, ScalarField::kLogMicrofacetDensity, ScalarField::kMicrofacetRoughness, ScalarField::kDensityRandomization }, false },
			{ Slot::kCoat, { ScalarField::kRoughness, ScalarField::kLevel }, true },
			{ Slot::kSubsurface, { ScalarField::kColor, ScalarField::kThickness }, true },
		};
		static_assert(std::size(rules) == kSlotCount);
		for (const auto& rule : rules) {
			Check(std::ranges::equal(ScalarsOf(rule.slot), rule.carried), std::format("scalars of '{}'", SlotName(rule.slot)));
			for (const auto field : fields) {
				const bool carried = std::ranges::find(rule.carried, field) != rule.carried.end();
				Check(ScalarRequired(rule.slot, field) == (carried && rule.required), std::format("'{}' requires '{}'", SlotName(rule.slot), ScalarFieldName(field)));
			}
		}

		// SlotsExclude: the four pairs, symmetric, never a slot with itself
		const std::pair<Slot, Slot> exclusive[]{ { Slot::kFuzz, Slot::kCoat }, { Slot::kFuzz, Slot::kSubsurface }, { Slot::kCoat, Slot::kSubsurface }, { Slot::kFuzz, Slot::kGlint } };
		for (const auto first : every) {
			for (const auto second : every) {
				const bool expected = std::ranges::any_of(exclusive, [&](const std::pair<Slot, Slot>& pair) {
					return (pair.first == first && pair.second == second) || (pair.first == second && pair.second == first);
				});
				Check(SlotsExclude(first, second) == expected && SlotsExclude(second, first) == expected, std::format("'{}' and '{}' exclude each other: {}", SlotName(first), SlotName(second), expected));
			}
		}

		// BlendAllowed: only `normal` is restricted, to the normal stack
		for (const auto slot : every) {
			for (const auto blend : blends) {
				Check(BlendAllowed(slot, blend) == (blend != Blend::kNormal || slot == Slot::kNormal), std::format("blend '{}' on '{}'", BlendName(blend), SlotName(slot)));
			}
		}

		// ChannelsOf, as CS reads each map (BSLightingShaderMaterialPBR.h)
		const std::pair<Slot, const char*> channels[]{
			{ Slot::kDiffuse, "rgba" }, { Slot::kEmissive, "rgb" }, { Slot::kRmaos, "rgba" }, { Slot::kNormal, "rgb" }, { Slot::kHeight, "r" },
			{ Slot::kFuzz, "rgba" }, { Slot::kGlint, "" }, { Slot::kCoat, "rgba" }, { Slot::kSubsurface, "rgba" }
		};
		static_assert(std::size(channels) == kSlotCount);
		for (const auto& [slot, expected] : channels) {
			Check(ChannelsOf(slot).ToString() == expected, std::format("channels of '{}' are '{}'", SlotName(slot), expected));
			Check(!SlotChannelNote(slot).empty(), std::format("'{}' has a channel note", SlotName(slot)));
		}
		Check(SlotChannelNote(Slot::kHeight).starts_with("r only") && SlotChannelNote(Slot::kGlint).starts_with("no texture"), "the height and glint notes state their exceptions");

		// BaseMapOf: the four slots that edit a map the material already has
		const std::pair<Slot, MaterialMap> baseMaps[]{
			{ Slot::kDiffuse, MaterialMap::kDiffuse }, { Slot::kEmissive, MaterialMap::kNone }, { Slot::kRmaos, MaterialMap::kRmaos }, { Slot::kNormal, MaterialMap::kNormal }, { Slot::kHeight, MaterialMap::kDisplacement },
			{ Slot::kFuzz, MaterialMap::kNone }, { Slot::kGlint, MaterialMap::kNone }, { Slot::kCoat, MaterialMap::kNone }, { Slot::kSubsurface, MaterialMap::kNone }
		};
		static_assert(std::size(baseMaps) == kSlotCount);
		for (const auto& [slot, expected] : baseMaps) {
			Check(BaseMapOf(slot) == expected, std::format("base map of '{}'", SlotName(slot)));
		}
		Check(BaseMapOf(static_cast<Slot>(99)) == MaterialMap::kNone && ScalarsOf(static_cast<Slot>(99)).empty() && SlotChannelNote(static_cast<Slot>(99)).empty() && !SlotsExclude(static_cast<Slot>(99), Slot::kFuzz), "an unknown slot answers every question as an empty row");

		// ScalarFallback: one value per field, what the menu fills in and the binding writes when the file leaves it out
		const std::pair<ScalarField, float> fallbacks[]{
			{ ScalarField::kStrength, 1.0f }, { ScalarField::kScale, 1.0f }, { ScalarField::kColor, 1.0f }, { ScalarField::kWeight, 1.0f },
			{ ScalarField::kScreenSpaceScale, 1.5f }, { ScalarField::kLogMicrofacetDensity, 40.0f }, { ScalarField::kMicrofacetRoughness, 0.015f }, { ScalarField::kDensityRandomization, 2.0f },
			{ ScalarField::kRoughness, 0.15f }, { ScalarField::kLevel, 0.6f }, { ScalarField::kThickness, 1.0f }
		};
		static_assert(std::size(fallbacks) == kScalarFieldCount);
		for (const auto& [field, expected] : fallbacks) {
			Check(ScalarFallback(field) == expected, std::format("fallback of '{}'", ScalarFieldName(field)));
		}
		Check(ScalarFallback(static_cast<ScalarField>(99)) == 0.0f && ScalarOf(static_cast<const SlotScalars&>(SlotScalars{}), static_cast<ScalarField>(99)) == nullptr, "an unknown field falls back to zero and reaches no member");

		// ScalarOf: every field but kColor reaches its own member
		SlotScalars sc;
		for (std::size_t i = 0; i < kScalarFieldCount; ++i) {
			const auto field = fields[i];
			std::optional<Param>* p = ScalarOf(sc, field);
			const std::optional<Param>* cp = ScalarOf(std::as_const(sc), field);
			if (field == ScalarField::kColor) {
				Check(p == nullptr && cp == nullptr, "ScalarOf is null for color");
				continue;
			}
			Check(p != nullptr && cp == p && !p->has_value(), std::format("ScalarOf '{}' reaches an empty member", ScalarFieldName(field)));
			if (p) {
				*p = Param{ static_cast<float>(i) };
			}
		}
		const auto holds = [](const std::optional<Param>& a_param, float a_value) {
			const float* v = a_param ? std::get_if<float>(&*a_param) : nullptr;
			return v && Near(*v, a_value);
		};
		Check(holds(sc.strength, 0.0f) && holds(sc.scale, 1.0f) && holds(sc.weight, 3.0f), "strength, scale and weight written through ScalarOf");
		Check(holds(sc.screenSpaceScale, 4.0f) && holds(sc.logMicrofacetDensity, 5.0f) && holds(sc.microfacetRoughness, 6.0f) && holds(sc.densityRandomization, 7.0f), "the four glint parameters written through ScalarOf");
		Check(holds(sc.roughness, 8.0f) && holds(sc.level, 9.0f) && holds(sc.thickness, 10.0f), "roughness, level and thickness written through ScalarOf");
		Check(!sc.color, "color untouched by ScalarOf");

		// ScalarFieldName and ParseScalarField round trip
		std::set<std::string_view> names;
		for (const auto field : fields) {
			const auto name = ScalarFieldName(field);
			names.insert(name);
			Check(!name.empty() && name != "?" && ParseScalarField(name) == field, std::format("'{}' round trips", name));
		}
		Check(names.size() == kScalarFieldCount, "scalar field names are distinct");
		Check(ParseScalarField("strength") == ScalarField::kStrength && ParseScalarField("thickness") == ScalarField::kThickness, "first and last names parse");
		Check(!ParseScalarField("") && !ParseScalarField("Strength") && !ParseScalarField("colour") && !ParseScalarField("strength "), "unknown, differently cased and padded names do not parse");
		Check(ScalarFieldName(static_cast<ScalarField>(99)) == "?", "an out-of-range field has a placeholder name");
	}

	void Resolution()
	{
		std::vector<Recipe> loaded;
		const auto          add = [&](const char* a_id, std::initializer_list<RecipeKey> a_keys, std::optional<int> a_priority = std::nullopt) {
            Recipe r;
            r.id = a_id;
            r.keys = a_keys;
            r.priority = a_priority;
            loaded.push_back(r);
		};
		add("fallback", { RecipeKey{ KeyKind::kDefault, {}, {} } });
		add("iron", { RecipeKey{ KeyKind::kMaterial, {}, "*/iron/*" } });
		add("ebony", { RecipeKey{ KeyKind::kKeyword, Form("0xAAA~Skyrim.esm"), {} } });
		add("cuirass", { RecipeKey{ KeyKind::kArmor, Form("0x12E49~Skyrim.esm"), {} } });
		add("magicka-shader", { RecipeKey{ KeyKind::kEffectShader, Form("0x92DED~Skyrim.esm"), {} } });
		add("fortify-magicka", { RecipeKey{ KeyKind::kEnchantment, Form("0xAC~Skyrim.esm"), {} } });
		add("magicka-effect", { RecipeKey{ KeyKind::kMagicEffect, Form("0xAB~Skyrim.esm"), {} } });
		add("magicka-shader-later", { RecipeKey{ KeyKind::kEffectShader, Form("0x92DED~Skyrim.esm"), {} } });
		add("low-effect", { RecipeKey{ KeyKind::kMagicEffect, Form("0xAB~Skyrim.esm"), {} } }, -5);
		add("unresolved", { RecipeKey{ KeyKind::kArmor, Form("ArmorIronCuirass"), {} } });

		WornPiece piece;
		piece.magicEffect = Key("0xAB~Skyrim.esm");
		piece.enchantment = Key("0xAC~Skyrim.esm");
		piece.effectShader = Key("0x92DED~Skyrim.esm");
		piece.armor = Key("0x12E49~Skyrim.esm");
		piece.keywords = { Key("0xAAA~Skyrim.esm") };
		piece.diffusePaths = { "textures\\armor\\iron\\cuirass_d.dds" };

		auto        resolved = Resolve(piece, loaded);
		std::string order;
		for (const auto& r : resolved) {
			order += r.recipe->id + " ";
		}
		Check(order == "low-effect iron ebony cuirass magicka-shader-later fortify-magicka ", "merge order by priority, later files own shared keys, default dropped, unresolved never matches: " + order);
		Check(resolved.size() > 4 && resolved[4].key.kind == KeyKind::kEffectShader && resolved[0].priority == -5 && resolved[2].priority == 20, "keys and priorities reported");

		WornPiece unenchanted;
		unenchanted.armor = Key("0x12E49~Skyrim.esm");
		unenchanted.diffusePaths = { "textures/armor/iron/boots_d.dds" };
		resolved = Resolve(unenchanted, loaded);
		order.clear();
		for (const auto& r : resolved) {
			order += r.recipe->id + " ";
		}
		Check(order == "fallback iron cuirass ", "unenchanted piece matches default, material and armor: " + order);
		Check(AnyUnenchantedKey(loaded) && !AnyUnenchantedKey(std::span{ loaded.data() + 4, 3 }), "unenchanted keys detected");
	}

	void Variants()
	{
		const auto recipe = Everything();
		const auto& iron = recipe.variants[0];
		const auto& body = recipe.variants[1];
		Check(VariantApplies(iron, Key("0x12E49~Skyrim.esm")) && !VariantApplies(iron, Key("0x12E4A~Skyrim.esm")), "armor-keyed variant");
		GeometryIdentity g{ std::nullopt, "MaleBody", "" };
		Check(VariantApplies(body, g) && !VariantApplies(iron, g) && !VariantApplies(body, Key("0x12E49~Skyrim.esm")), "selector variant");
		const auto applied = ApplyVariant(recipe, iron);
		const auto* one = applied.FindSignal("one");
		const auto* red = applied.FindSignal("red");
		Check(one && Get<ConstantSignal>(one->kind)->value == Value{ 0.5f }, "scalar override applied");
		Check(red && Get<ConstantSignal>(red->kind)->value == Value{ Vec3{ 0, 1, 0 } }, "colour override applied");
		Check(applied.outputs == recipe.outputs && applied.sources == recipe.sources, "a variant changes no structure");
		const auto* pulse = ApplyVariant(recipe, body).FindSignal("pulse");
		Check(pulse && Is<ConstantSignal>(pulse->kind) && !pulse->curve, "an override turns any signal into a constant");
	}

	void Classification()
	{
		Recipe r;
		r.keys = { RecipeKey{ KeyKind::kDefault, {}, {} } };
		r.signals.push_back(Const("one", 1.0f));
		r.signals.push_back(Const("half", 0.5f));
		r.signals.push_back(Colour("red", Vec3{ 1, 0, 0 }));
		r.signals.push_back(Expr("fixed", "@one * 2 + @half"));
		r.signals.push_back(Expr("clock", "frac(time)"));
		r.signals.push_back(Signal{ "pulse", PulseSignal{}, std::nullopt });
		r.signals.push_back(Signal{ "steady", SmoothSignal{ At("one"), 1.0f }, std::nullopt });
		r.signals.push_back(Signal{ "eased", SmoothSignal{ At("pulse"), 1.0f }, std::nullopt });
		r.signals.push_back(Signal{ "curved", ConstantSignal{ 0.2f }, CurveRef{ "@flash" } });
		r.curves.push_back({ "flash", "1 - x" });
		ImageSource still;
		still.path = "a.dds";
		ImageSource scrolled = still;
		scrolled.scroll = Vec2Param{ std::array<Param, 2>{ 0.0f, At("clock") } };
		ImageSource offset = still;
		offset.scroll = Vec2Param{ std::array<Param, 2>{ 0.25f, 0.0f } };
		r.sources.push_back({ "still", still });
		r.sources.push_back({ "scrolled", scrolled });
		r.sources.push_back({ "offset", offset });
		r.sources.push_back({ "metallic", MaterialSource{ MaterialChannel::kMetallic } });
		r.sources.push_back({ "ring", RippleSource{ At("pulse"), 1.0f, 1.0f, 1.0f } });
		r.masks.push_back({ "fixedMask", "@metallic > 0.4" });
		r.masks.push_back({ "live", "@metallic > @pulse" });
		r.masks.push_back({ "chained", "@live * 2" });
		r.masks.push_back({ "timed", "@metallic * frac(time)" });

		Check(!IsAnimated(r, "one") && !IsAnimated(r, "fixed") && !IsAnimated(r, "steady") && !IsAnimated(r, "curved"), "constants, expressions over constants and smoothed constants are static");
		Check(IsAnimated(r, "clock") && IsAnimated(r, "pulse") && IsAnimated(r, "eased"), "time, pulses and smoothed pulses are animated");
		Check(!IsAnimated(r, "missing"), "an unknown signal is static");
		Check(!IsAnimated(r, r.sources[0]) && !IsAnimated(r, r.sources[2]) && !IsAnimated(r, r.sources[3]), "still images, constant offsets and material channels are static");
		Check(IsAnimated(r, r.sources[1]) && IsAnimated(r, r.sources[4]), "scrolled images and ripples are animated");
		Check(!IsAnimated(r, r.masks[0]) && IsAnimated(r, r.masks[1]) && IsAnimated(r, r.masks[2]) && IsAnimated(r, r.masks[3]), "masks follow what they read");

		const auto stack = [&](const char* a_source, Param a_opacity, const char* a_mask) {
			MaterialOutput o;
			o.slot = Slot::kEmissive;
			o.scalars.strength = At("one");
			Layer l;
			l.source = At(a_source);
			l.opacity = std::move(a_opacity);
			if (a_mask) l.mask = At(a_mask);
			o.stack.push_back(l);
			return Output{ o };
		};
		Check(!IsAnimated(r, stack("still", 1.0f, "fixedMask")), "a stack of constants is static");
		Check(IsAnimated(r, stack("scrolled", 1.0f, nullptr)), "a scrolling source animates the stack");
		Check(IsAnimated(r, stack("still", At("pulse"), nullptr)), "a live opacity animates the stack");
		Check(IsAnimated(r, stack("still", 1.0f, "live")), "a live mask animates the stack");
		Check(IsAnimated(r, stack("live", 1.0f, nullptr)), "a live mask as the source animates the stack");
		LightOutput light;
		light.color = Vec3Param{ At("red") };
		light.intensity = At("one");
		Check(!IsAnimated(r, Output{ light }), "a light with constant colour and intensity is static");
		light.intensity = At("pulse");
		Check(IsAnimated(r, Output{ light }), "a light with a pulsing intensity is animated");
	}

	// The menu's text forms read back what they print, and reject garbage.
	void TextForms()
	{
		using namespace test;
		Check(ParamText(Param{ 0.25f }) == "0.25", "a constant prints without trailing zeros");
		Check(ParamText(Param{ 2.0f }) == "2", "a whole constant prints as an integer");
		Check(ParamText(Param{ Ref{ "glow" } }) == "@glow", "a reference prints with its sigil");
		const auto p = ParseParam(" @glow ");
		Check(p && Get<Ref>(*p) && Get<Ref>(*p)->name == "glow", "a reference parses around spaces");
		const auto f = ParseParam("0.5");
		Check(f && Get<float>(*f) && Near(*Get<float>(*f), 0.5f), "a number parses");
		Check(!ParseParam("@"), "a bare sigil is not a reference");
		Check(!ParseParam("abc"), "a word is not a parameter");
		Check(!ParseParam("1 2"), "two numbers are not a parameter");
		const auto colour = ParseVec3Param("1, 0.5, @blue");
		Check(colour && Get<std::array<Param, 3>>(*colour), "a colour of three parts parses");
		if (colour) {
			Check(Vec3ParamText(*colour) == "1, 0.5, @blue", "a colour prints back the same");
		}
		const auto named = ParseVec3Param("@hue");
		Check(named && Get<Ref>(*named), "a colour reference parses");
		Check(!ParseVec3Param("1, 2"), "two parts are not a colour");
		const auto broadcast = ParseVec3Param("0.5");
		Check(broadcast && Get<std::array<Param, 3>>(*broadcast) && (*Get<std::array<Param, 3>>(*broadcast))[0] == Param{ 0.5f } && (*Get<std::array<Param, 3>>(*broadcast))[2] == Param{ 0.5f }, "one number broadcasts to every component");
		Check(!ParseVec3Param("x"), "a word is not a colour");
		const auto source = ParseLayerSource("0.2, 0.4, 0.6");
		const auto grey = ParseLayerSource("0");
		Check(grey && Get<Vec3>(*grey) && *Get<Vec3>(*grey) == Vec3{ 0.0f, 0.0f, 0.0f }, "one number in a layer source is a grey colour");
		Check(!ParseLayerSource("grey"), "a word is not a layer source");
		Check(source && Get<Vec3>(*source) && Near(Get<Vec3>(*source)->z, 0.6f), "a constant layer source parses");
		Check(LayerSourceText(LayerSource{ Ref{ "fill" } }) == "@fill", "a source reference prints");
		Check(ParseBlend("multiply") == Blend::kMultiply, "a blend parses by name");
		Check(!ParseBlend("mix"), "an unknown blend is rejected");
		const auto blends = { Blend::kReplace, Blend::kMultiply, Blend::kAdd, Blend::kSubtract, Blend::kScreen, Blend::kLerp, Blend::kNormal };
		for (const auto b : blends) {
			Check(ParseBlend(BlendName(b)) == b, "every blend name round-trips");
		}
	}

	// Every recipe the build ships must load without a row error.
	void ShippedRecipes()
	{
		using namespace test;
		const auto      root = Fixtures() / ".." / ".." / "recipes";
		std::error_code ec;
		std::size_t     files = 0;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
			if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
				continue;
			}
			++files;
			std::ifstream     in(entry.path(), std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			const auto result = ParseRecipe(text.str(), entry.path().stem().string());
			Check(result.recipe.has_value(), "shipped recipe reads: " + entry.path().filename().string());
			for (const auto& d : result.diagnostics) {
				Check(d.severity != Severity::kError, entry.path().filename().string() + " " + d.where + ": " + d.message);
			}
		}
		Check(files > 0, "the recipes folder has files");
	}
}

int main()
{
	Forms();
	TextForms();
	ShippedRecipes();
	Globs();
	CanonicalFile();
	RoundTrip();
	AnalysisKinds();
	Reading();
	Validation();
	Words();
	SlotRules();
	Resolution();
	Variants();
	Classification();
	return test::Finish("recipe");
}
