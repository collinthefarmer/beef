// Every RecipeEdit applied to the canonical recipe: the change lands where
// it should, an edit that does not fit leaves the recipe untouched and says
// why, and the result serialises as the file would be written by hand.

#include "Edits.h"
#include "test_support.h"

#include <filesystem>
#include <string>

using namespace WornEnchantmentPBR;
using namespace WornEnchantmentPBR::Studio;
using test::Check;

namespace
{
	// The canonical file, parsed once; every test edits a copy.
	Recipe Canonical()
	{
		const auto path = std::filesystem::path{ WEPBR_FIXTURES_DIR }.parent_path().parent_path() / "schema" / "example-magicka.json";
		const auto loaded = ParseRecipe(test::ReadFile(path), "example-magicka");
		return loaded.recipe.value_or(Recipe{});
	}

	Ref At(const char* a_name)
	{
		return Ref{ a_name };
	}

	const MaterialOutput* MaterialAt(const Recipe& a_recipe, std::size_t a_output)
	{
		return a_output < a_recipe.outputs.size() ? Get<MaterialOutput>(a_recipe.outputs[a_output]) : nullptr;
	}

	const Layer* LayerAt(const Recipe& a_recipe, std::size_t a_output, std::size_t a_layer)
	{
		const auto* material = MaterialAt(a_recipe, a_output);
		return material && a_layer < material->stack.size() ? &material->stack[a_layer] : nullptr;
	}

	// The edit lands: Apply accepts it and the recipe changed.
	void Accepted(Recipe& a_recipe, const RecipeEdit& a_edit, const std::string& a_what)
	{
		const auto problem = Apply(a_recipe, a_edit);
		Check(!problem, a_what + " is accepted" + (problem ? ": " + problem->where + ": " + problem->message : ""));
	}

	// The edit is refused with the expected row and reason, and the recipe is
	// what it was before.
	void Refused(const Recipe& a_recipe, const RecipeEdit& a_edit, const std::string& a_where, const std::string& a_fragment, const std::string& a_what)
	{
		Recipe     copy = a_recipe;
		const auto problem = Apply(copy, a_edit);
		Check(problem.has_value(), a_what + " is refused");
		if (problem) {
			Check(problem->where == a_where, a_what + ": where is '" + problem->where + "', expected '" + a_where + "'");
			Check(problem->message.find(a_fragment) != std::string::npos, a_what + ": message '" + problem->message + "' mentions '" + a_fragment + "'");
		}
		Check(copy == a_recipe, a_what + " leaves the recipe unchanged");
	}

	bool Contains(const std::string& a_text, const std::string& a_fragment)
	{
		return a_text.find(a_fragment) != std::string::npos;
	}

	void LayerEdits()
	{
		Recipe r = Canonical();
		Accepted(r, SetLayerSource{ 0, 1, At("sheenField") }, "layer source to a source");
		const auto* source = LayerAt(r, 0, 1) ? Get<Ref>(LayerAt(r, 0, 1)->source) : nullptr;
		Check(source && source->name == "sheenField", "layer source written");
		Accepted(r, SetLayerSource{ 0, 1, At("metal") }, "layer source to a mask");
		Accepted(r, SetLayerSource{ 0, 1, Vec3{ 0.5f, 0.25f, 1.0f } }, "layer source to a colour");
		Check(LayerAt(r, 0, 1) && LayerAt(r, 0, 1)->source == LayerSource{ Vec3{ 0.5f, 0.25f, 1.0f } }, "layer colour source written");
		Refused(r, SetLayerSource{ 0, 1, At("nothing") }, "output 0 layer 1", "unknown source or mask '@nothing'", "unknown layer source");

		Accepted(r, SetLayerCurve{ 2, 1, CurveRef{ "@punchy" } }, "layer curve to a declared curve");
		Check(LayerAt(r, 2, 1) && LayerAt(r, 2, 1)->curve == CurveRef{ "@punchy" }, "layer curve written");
		Accepted(r, SetLayerCurve{ 2, 1, CurveRef{ "x * 2" } }, "layer curve to an inline expression");
		Accepted(r, SetLayerCurve{ 2, 0, std::nullopt }, "layer curve cleared");
		Check(LayerAt(r, 2, 0) && !LayerAt(r, 2, 0)->curve, "layer curve cleared in the row");
		Refused(r, SetLayerCurve{ 2, 1, CurveRef{ "@missing" } }, "output 2 layer 1", "unknown curve '@missing'", "unknown layer curve");
		Refused(r, SetLayerCurve{ 2, 1, CurveRef{ "" } }, "output 2 layer 1", "empty", "empty layer curve");

		Accepted(r, SetLayerBlend{ 0, 0, Blend::kScreen }, "layer blend");
		Check(LayerAt(r, 0, 0) && LayerAt(r, 0, 0)->blend == Blend::kScreen, "layer blend written");
		Refused(r, SetLayerBlend{ 0, 0, Blend::kNormal }, "output 0 layer 0", "normal slot", "normal blend on emissive");

		Accepted(r, SetLayerOpacity{ 3, 0, 0.35f }, "layer opacity to a number");
		Check(LayerAt(r, 3, 0) && LayerAt(r, 3, 0)->opacity == Param{ 0.35f }, "layer opacity written");
		Accepted(r, SetLayerOpacity{ 3, 0, At("fillLevel") }, "layer opacity to a signal");
		Refused(r, SetLayerOpacity{ 3, 0, At("noSignal") }, "output 3 layer 0", "unknown signal '@noSignal'", "opacity naming no signal");

		Accepted(r, SetLayerColor{ 1, 0, Vec3Param{ At("edgeColor") } }, "layer colour to a signal");
		Check(LayerAt(r, 1, 0) && LayerAt(r, 1, 0)->color == Vec3Param{ At("edgeColor") }, "layer colour written");
		Accepted(r, SetLayerColor{ 1, 0, Vec3Param{ std::array<Param, 3>{ 1.0f, At("glowStrength"), 0.0f } } }, "layer colour with a signal part");
		Accepted(r, SetLayerColor{ 0, 0, std::nullopt }, "layer colour cleared");
		Check(LayerAt(r, 0, 0) && !LayerAt(r, 0, 0)->color, "layer colour cleared in the row");
		Refused(r, SetLayerColor{ 1, 0, Vec3Param{ At("noHue") } }, "output 1 layer 0", "unknown signal '@noHue'", "colour naming no signal");
		Refused(r, SetLayerColor{ 1, 0, Vec3Param{ std::array<Param, 3>{ 1.0f, At("nope"), 0.0f } } }, "output 1 layer 0", "unknown signal '@nope'", "colour part naming no signal");

		Accepted(r, SetLayerMask{ 0, 1, At("metal") }, "layer mask");
		Check(LayerAt(r, 0, 1) && LayerAt(r, 0, 1)->mask == std::optional<Ref>{ At("metal") }, "layer mask written");
		Accepted(r, SetLayerMask{ 0, 0, std::nullopt }, "layer mask cleared");
		Check(LayerAt(r, 0, 0) && !LayerAt(r, 0, 0)->mask, "layer mask cleared in the row");
		Refused(r, SetLayerMask{ 0, 1, At("metallic") }, "output 0 layer 1", "unknown mask '@metallic'", "a source name as a mask");

		Accepted(r, SetLayerChannels{ 1, 0, ChannelSet{ true, false, false, true } }, "layer channels");
		Check(LayerAt(r, 1, 0) && LayerAt(r, 1, 0)->channels == ChannelSet{ true, false, false, true }, "layer channels written");
		Refused(r, SetLayerChannels{ 1, 0, ChannelSet{ false, false, false, false } }, "output 1 layer 0", "selects nothing", "empty channel set");

		Refused(r, SetLayerBlend{ 0, 3, Blend::kAdd }, "output 0 layer 3", "3 layers", "layer index past the end");
		Refused(r, SetLayerBlend{ 4, 0, Blend::kAdd }, "output 4", "output 4 is a light", "layer edit on the light");
		Refused(r, SetLayerBlend{ 5, 0, Blend::kAdd }, "output 5", "5 outputs", "layer edit past the outputs");
	}

	void StackEdits()
	{
		Recipe r = Canonical();
		Layer  layer = DefaultLayer();
		Check(Get<Vec3>(layer.source) && *Get<Vec3>(layer.source) == Vec3{ 1.0f, 1.0f, 1.0f } && layer.blend == Blend::kReplace && layer.opacity == Param{ 1.0f } && !layer.mask && !layer.curve && !layer.color, "default layer is white replace at full opacity");

		Accepted(r, AddLayer{ 3, layer, std::nullopt }, "add layer on top");
		Check(MaterialAt(r, 3) && MaterialAt(r, 3)->stack.size() == 2 && MaterialAt(r, 3)->stack[1] == layer, "added layer is last");
		layer.source = At("fill");
		Accepted(r, AddLayer{ 0, layer, 1 }, "add layer at an index");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->stack.size() == 4 && MaterialAt(r, 0)->stack[1] == layer, "inserted layer sits at 1");
		const auto* moved = LayerAt(r, 0, 2);
		Check(moved && Get<Ref>(moved->source) && Get<Ref>(moved->source)->name == "ring", "the former layer 1 moved up to 2");
		Accepted(r, AddLayer{ 0, layer, 4 }, "add layer at the end index");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->stack.size() == 5, "at == size appends");
		Refused(r, AddLayer{ 0, layer, 6 }, "output 0 layer 6", "5 layers", "add layer past the end");
		Layer broken = layer;
		broken.source = At("ghost");
		Refused(r, AddLayer{ 0, broken, std::nullopt }, "output 0 layer 5", "unknown source or mask '@ghost'", "add layer with an unknown source");
		broken = layer;
		broken.blend = Blend::kNormal;
		Refused(r, AddLayer{ 0, broken, std::nullopt }, "output 0 layer 5", "normal slot", "add layer with the normal blend on emissive");
		Refused(r, AddLayer{ 4, layer, std::nullopt }, "output 4", "is a light", "add layer to the light");

		Accepted(r, RemoveLayer{ 0, 1 }, "remove layer");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->stack.size() == 4 && LayerAt(r, 0, 1) && Get<Ref>(LayerAt(r, 0, 1)->source) && Get<Ref>(LayerAt(r, 0, 1)->source)->name == "ring", "layer removed and the rest closed up");
		Refused(r, RemoveLayer{ 0, 4 }, "output 0 layer 4", "4 layers", "remove layer past the end");

		// Stack 0 is now fill, ring, stepRing, fill (the inserted and appended layers both read @fill).
		auto NameAt = [&](std::size_t a_layer) -> std::string {
			const auto* row = LayerAt(r, 0, a_layer);
			const auto* ref = row ? Get<Ref>(row->source) : nullptr;
			return ref ? ref->name : (row ? "colour" : "?");
		};
		Accepted(r, MoveLayer{ 0, 0, 2 }, "move layer up");
		Check(NameAt(0) == "ring" && NameAt(1) == "stepRing" && NameAt(2) == "fill" && NameAt(3) == "fill", "layer moved up ends at 2: " + NameAt(0) + " " + NameAt(1) + " " + NameAt(2) + " " + NameAt(3));
		Accepted(r, MoveLayer{ 0, 3, 0 }, "move layer down");
		Check(NameAt(0) == "fill" && NameAt(1) == "ring" && NameAt(2) == "stepRing" && NameAt(3) == "fill", "layer moved down ends at 0");
		Accepted(r, MoveLayer{ 0, 1, 1 }, "move layer onto itself");
		Check(NameAt(1) == "ring", "moving onto itself changes nothing");
		Refused(r, MoveLayer{ 0, 4, 0 }, "output 0 layer 4", "4 layers", "move from past the end");
		Refused(r, MoveLayer{ 0, 0, 4 }, "output 0 layer 0", "4 layers", "move to past the end");

		Refused(r, ClearLayers{ 4 }, "output 4", "is a light", "clear layers of the light");
		Refused(r, ClearLayers{ 7 }, "output 7", "5 outputs", "clear layers past the end");
		Accepted(r, ClearLayers{ 0 }, "clear layers");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->stack.empty(), "the cleared stack is empty");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->scalars.strength.has_value(), "clearing the layers keeps the output's scalars");
		Check(MaterialAt(r, 1) && MaterialAt(r, 1)->stack.size() == 2, "other stacks are untouched");
		Accepted(r, ClearLayers{ 0 }, "clear an empty stack");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->stack.empty(), "clearing an empty stack leaves it empty");
	}

	void OutputEdits()
	{
		const auto emissive = DefaultOutput(Surface::kShell, Slot::kEmissive);
		Check(emissive.surface == Surface::kShell && emissive.slot == Slot::kEmissive && emissive.stack.empty(), "default output is an empty stack on the slot");
		Check(emissive.scalars.strength == std::optional<Param>{ 1.0f } && !emissive.scalars.color && !emissive.scalars.weight, "default emissive has strength 1 only");
		Check(DefaultOutput(Surface::kMaterial, Slot::kHeight).scalars.scale == std::optional<Param>{ 1.0f }, "default height has scale 1");
		const auto fuzz = DefaultOutput(Surface::kMaterial, Slot::kFuzz);
		Check(fuzz.scalars.color == std::optional<Vec3Param>{ std::array<Param, 3>{ 1.0f, 1.0f, 1.0f } } && fuzz.scalars.weight == std::optional<Param>{ 1.0f }, "default fuzz is white at weight 1");
		const auto coat = DefaultOutput(Surface::kMaterial, Slot::kCoat);
		Check(coat.scalars.roughness == std::optional<Param>{ 0.15f } && coat.scalars.level == std::optional<Param>{ 0.6f }, "default coat has roughness 0.15 and level 0.6");
		const auto subsurface = DefaultOutput(Surface::kMaterial, Slot::kSubsurface);
		Check(subsurface.scalars.color == std::optional<Vec3Param>{ std::array<Param, 3>{ 1.0f, 1.0f, 1.0f } } && subsurface.scalars.thickness == std::optional<Param>{ 1.0f }, "default subsurface is white at thickness 1");
		const auto glint = DefaultOutput(Surface::kMaterial, Slot::kGlint);
		Check(glint.scalars == SlotScalars{}, "default glint sets no scalar: all four are optional");
		Check(DefaultOutput(Surface::kMaterial, Slot::kRmaos).scalars == SlotScalars{}, "default rmaos has no scalars");

		Recipe r = Canonical();
		Accepted(r, AddOutput{ Surface::kMaterial, Slot::kCoat }, "add a coat output on the material");
		Check(r.outputs.size() == 6 && MaterialAt(r, 5) && *MaterialAt(r, 5) == coat, "the added output is the default coat, appended");
		Accepted(r, AddOutput{ Surface::kShell, Slot::kDiffuse }, "add a diffuse output on the PBR-copy shell");
		Refused(r, AddOutput{ Surface::kMaterial, Slot::kFuzz }, "output 7", "output 5 on 'coat' excludes 'fuzz'", "fuzz beside coat on the material");
		Refused(r, AddOutput{ Surface::kMaterial, Slot::kSubsurface }, "output 7", "excludes 'subsurface'", "subsurface beside coat on the material");
		Refused(r, AddOutput{ Surface::kShell, Slot::kGlint }, "output 7", "output 1 on 'fuzz' excludes 'glint'", "glint beside fuzz on the shell");
		Accepted(r, AddOutput{ Surface::kMaterial, Slot::kGlint }, "glint beside coat on the material coexists");
		Refused(r, AddOutput{ Surface::kShell, Slot::kCoat }, "output 8", "output 1 on 'fuzz' excludes 'coat'", "coat beside fuzz on the shell (they share one feature)");

		Recipe vanilla = Canonical();
		vanilla.shell.material = ShellMaterial::kVanilla;
		Refused(vanilla, AddOutput{ Surface::kShell, Slot::kDiffuse }, "output 5", "vanilla shell offers no 'diffuse'", "diffuse on a vanilla shell");
		Accepted(vanilla, AddOutput{ Surface::kMaterial, Slot::kDiffuse }, "diffuse on the material beside a vanilla shell");

		Accepted(r, RemoveOutput{ 4 }, "remove the light");
		Check(r.outputs.size() == 7 && MaterialAt(r, 4) && MaterialAt(r, 4)->slot == Slot::kCoat, "outputs closed up over the removed light");
		Refused(r, RemoveOutput{ 7 }, "output 7", "7 outputs", "remove output past the end");
	}

	void ScalarEdits()
	{
		Recipe r = Canonical();
		Accepted(r, SetScalar{ 0, ScalarField::kStrength, 2.0f }, "emissive strength to a number");
		Check(MaterialAt(r, 0) && MaterialAt(r, 0)->scalars.strength == std::optional<Param>{ 2.0f }, "strength written");
		Accepted(r, SetScalar{ 2, ScalarField::kScale, At("fillLevel") }, "height scale to a signal");
		Check(MaterialAt(r, 2) && MaterialAt(r, 2)->scalars.scale == std::optional<Param>{ At("fillLevel") }, "scale written");
		Accepted(r, SetScalar{ 1, ScalarField::kWeight, 0.5f }, "fuzz weight");
		Refused(r, SetScalar{ 2, ScalarField::kWeight, 1.0f }, "output 2", "slot 'height' has no 'weight'", "weight on height");
		Refused(r, SetScalar{ 3, ScalarField::kStrength, 1.0f }, "output 3", "slot 'rmaos' has no 'strength'", "strength on rmaos");
		Refused(r, SetScalar{ 1, ScalarField::kColor, 1.0f }, "output 1", "colour", "the colour through SetScalar");
		Refused(r, SetScalar{ 0, ScalarField::kStrength, At("nothing") }, "output 0", "unknown signal '@nothing'", "strength naming no signal");
		Refused(r, SetScalar{ 4, ScalarField::kStrength, 1.0f }, "output 4", "is a light", "scalar on the light");

		Accepted(r, SetColorScalar{ 1, Vec3Param{ At("glowHue") } }, "fuzz colour to a signal");
		Check(MaterialAt(r, 1) && MaterialAt(r, 1)->scalars.color == std::optional<Vec3Param>{ At("glowHue") }, "fuzz colour written");
		Accepted(r, SetColorScalar{ 1, Vec3Param{ std::array<Param, 3>{ 0.2f, 0.3f, 0.4f } } }, "fuzz colour to numbers");
		Refused(r, SetColorScalar{ 0, Vec3Param{ At("glowHue") } }, "output 0", "slot 'emissive' has no 'color'", "colour on emissive");
		Refused(r, SetColorScalar{ 1, Vec3Param{ At("noHue") } }, "output 1", "unknown signal '@noHue'", "fuzz colour naming no signal");
		Refused(r, SetColorScalar{ 4, Vec3Param{ At("glowHue") } }, "output 4", "is a light", "colour scalar on the light");
	}

	void SignalEdits()
	{
		Recipe r = Canonical();
		Accepted(r, SetConstant{ "glowLevel", 0.75f }, "pin an expression to a constant");
		const auto* glowLevel = r.FindSignal("glowLevel");
		Check(glowLevel && glowLevel->kind == SignalKind{ ConstantSignal{ 0.75f } }, "expr replaced by the constant");
		Accepted(r, SetConstant{ "fillLevel", 1.0f }, "pin an efsh signal to a constant");
		const auto* fillLevel = r.FindSignal("fillLevel");
		Check(fillLevel && Get<ConstantSignal>(fillLevel->kind) && fillLevel->curve == CurveRef{ "@rest" }, "efsh replaced by a constant, curve kept");
		Accepted(r, SetConstant{ "glowHue", Vec3{ 1.0f, 0.0f, 0.5f } }, "colour constant");
		const auto* glowHue = r.FindSignal("glowHue");
		Check(glowHue && glowHue->kind == SignalKind{ ConstantSignal{ Vec3{ 1.0f, 0.0f, 0.5f } } }, "colour constant written");
		Refused(r, SetConstant{ "unknown", 1.0f }, "signal unknown", "no such signal", "constant on a missing signal");

		Accepted(r, SetExpression{ "glowStrength", "@fillLevel * 2" }, "expression over a constant");
		const auto* glowStrength = r.FindSignal("glowStrength");
		Check(glowStrength && glowStrength->kind == SignalKind{ ExprSignal{ "@fillLevel * 2" } }, "expression written");
		Refused(r, SetExpression{ "glowStrength", "" }, "signal glowStrength", "empty", "empty expression");
		Refused(r, SetExpression{ "unknown", "1" }, "signal unknown", "no such signal", "expression on a missing signal");

		Accepted(r, SetSignalCurve{ "glowStrength", CurveRef{ "@flash" } }, "signal curve to a declared curve");
		Check(glowStrength && glowStrength->curve == CurveRef{ "@flash" }, "signal curve written");
		Accepted(r, SetSignalCurve{ "glowStrength", CurveRef{ "1 - x" } }, "signal curve to an inline expression");
		Accepted(r, SetSignalCurve{ "fillLevel", std::nullopt }, "signal curve cleared");
		Check(fillLevel && !fillLevel->curve, "signal curve cleared in the row");
		Refused(r, SetSignalCurve{ "glowStrength", CurveRef{ "@missing" } }, "signal glowStrength", "unknown curve '@missing'", "signal curve naming no curve");
		Refused(r, SetSignalCurve{ "unknown", std::nullopt }, "signal unknown", "no such signal", "curve on a missing signal");

		Accepted(r, SetCurve{ "rest", "x / 0.1" }, "curve text");
		const auto* rest = r.FindCurve("rest");
		Check(rest && rest->text == "x / 0.1", "curve text written");
		Refused(r, SetCurve{ "rest", "" }, "curve rest", "empty", "empty curve text");
		Refused(r, SetCurve{ "unknown", "x" }, "curve unknown", "no such curve", "text on a missing curve");

		Accepted(r, SetMask{ "metal", "@metallic * 0.5" }, "mask text");
		const auto* metal = r.FindMask("metal");
		Check(metal && metal->text == "@metallic * 0.5", "mask text written");
		Refused(r, SetMask{ "metal", "" }, "mask metal", "empty", "empty mask text");
		Refused(r, SetMask{ "unknown", "1" }, "mask unknown", "no such mask", "text on a missing mask");
	}

	void Serialised()
	{
		Recipe r = Canonical();
		Accepted(r, SetLayerBlend{ 0, 0, Blend::kAdd }, "blend for serialisation");
		Accepted(r, SetLayerOpacity{ 2, 1, At("glossAmount") }, "opacity for serialisation");
		Accepted(r, SetScalar{ 0, ScalarField::kStrength, 2.5f }, "strength for serialisation");
		Accepted(r, SetConstant{ "glowLevel", 0.75f }, "constant for serialisation");
		Accepted(r, SetExpression{ "glowStrength", "@fillLevel * 2" }, "expression for serialisation");
		Accepted(r, AddOutput{ Surface::kMaterial, Slot::kCoat }, "output for serialisation");
		Accepted(r, SetLayerChannels{ 1, 0, ChannelSet{ true, false, false, true } }, "channels for serialisation");
		const auto text = SerializeRecipe(r);
		Check(Contains(text, "\"blend\": \"add\""), "blend add is written");
		Check(Contains(text, "\"opacity\": \"@glossAmount\""), "opacity reference is written");
		Check(Contains(text, "\"strength\": 2.5"), "strength is written");
		Check(Contains(text, "\"glowLevel\": {\n      \"constant\": 0.75"), "the pinned constant replaced the expression");
		Check(Contains(text, "\"expr\": \"@fillLevel * 2\""), "the expression is written");
		Check(Contains(text, "\"slot\": \"coat\"") && Contains(text, "\"roughness\": 0.15") && Contains(text, "\"level\": 0.6"), "the coat output is written with its defaults");
		Check(Contains(text, "\"channels\": \"ra\""), "channels are written");
		const auto again = ParseRecipe(text, "example-magicka");
		Check(again.recipe && *again.recipe == r, "the edited recipe survives a round trip");
	}

	void Described()
	{
		Check(Describe(SetLayerBlend{ 2, 0, Blend::kAdd }) == "output 2 layer 0: blend add", "describe blend: " + Describe(SetLayerBlend{ 2, 0, Blend::kAdd }));
		Check(Describe(SetLayerSource{ 0, 1, At("fill") }) == "output 0 layer 1: source @fill", "describe source");
		Check(Describe(SetLayerOpacity{ 0, 1, 0.35f }) == "output 0 layer 1: opacity 0.35", "describe opacity");
		Check(Describe(SetLayerColor{ 0, 1, std::nullopt }) == "output 0 layer 1: color none", "describe cleared colour");
		Check(Describe(SetLayerMask{ 0, 1, At("metal") }) == "output 0 layer 1: mask @metal", "describe mask");
		Check(Describe(SetLayerCurve{ 0, 1, CurveRef{ "@crisp" } }) == "output 0 layer 1: curve @crisp", "describe curve");
		Check(Describe(SetLayerChannels{ 0, 1, ChannelSet{ true, false, false, false } }) == "output 0 layer 1: channels r", "describe channels");
		Check(Describe(AddLayer{ 3, DefaultLayer(), std::nullopt }) == "output 3: add layer on top", "describe add layer on top");
		Check(Describe(AddLayer{ 3, DefaultLayer(), 1 }) == "output 3 layer 1: add layer", "describe add layer at");
		Check(Describe(RemoveLayer{ 3, 1 }) == "output 3 layer 1: remove", "describe remove layer");
		Check(Describe(MoveLayer{ 3, 1, 0 }) == "output 3 layer 1: move to 0", "describe move layer");
		Check(Describe(ClearLayers{ 3 }) == "output 3: clear layers", "describe clear layers");
		Check(Describe(AddOutput{ Surface::kShell, Slot::kFuzz }) == "outputs: add shell fuzz", "describe add output");
		Check(Describe(RemoveOutput{ 4 }) == "output 4: remove", "describe remove output");
		Check(Describe(SetScalar{ 2, ScalarField::kScale, At("heightScale") }) == "output 2: scale @heightScale", "describe scalar");
		Check(Describe(SetColorScalar{ 1, Vec3Param{ std::array<Param, 3>{ 1.0f, 0.5f, 0.0f } } }) == "output 1: color 1, 0.5, 0", "describe colour scalar");
		Check(Describe(SetConstant{ "glowStrength", 1.5f }) == "signal glowStrength: constant 1.5", "describe constant");
		Check(Describe(SetConstant{ "glowHue", Vec3{ 1.0f, 0.0f, 0.5f } }) == "signal glowHue: constant 1, 0, 0.5", "describe colour constant");
		Check(Describe(SetExpression{ "glowLevel", "@a * 2" }) == "signal glowLevel: expr @a * 2", "describe expression");
		Check(Describe(SetSignalCurve{ "fillLevel", std::nullopt }) == "signal fillLevel: curve none", "describe cleared signal curve");
		Check(Describe(SetCurve{ "rest", "x / 0.05" }) == "curve rest: x / 0.05", "describe curve text");
		Check(Describe(SetMask{ "metal", "@metallic" }) == "mask metal: @metallic", "describe mask text");
	}
}

int main()
{
	Check(!Canonical().outputs.empty(), "schema/example-magicka.json parses");
	LayerEdits();
	StackEdits();
	OutputEdits();
	ScalarEdits();
	SignalEdits();
	Serialised();
	Described();
	return test::Finish("edits");
}
