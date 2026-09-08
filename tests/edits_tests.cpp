#include "Edits.h"
#include "History.h"
#include "Expression.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <string>

using namespace WornEnchantmentPBR;
using namespace WornEnchantmentPBR::Studio;
using test::Check;

namespace
{
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

	const SurfaceOutput* MaterialAt(const Recipe& a_recipe, std::size_t a_output)
	{
		return a_output < a_recipe.outputs.size() ? Get<SurfaceOutput>(a_recipe.outputs[a_output]) : nullptr;
	}

	const Layer* LayerAt(const Recipe& a_recipe, std::size_t a_output, std::size_t a_layer)
	{
		const auto* material = MaterialAt(a_recipe, a_output);
		return material && a_layer < material->stack.size() ? &material->stack[a_layer] : nullptr;
	}

	void Accepted(Recipe& a_recipe, const RecipeEdit& a_edit, const std::string& a_what)
	{
		const Recipe before = a_recipe;
		const auto   problem = Apply(a_recipe, a_edit);
		Check(!problem, a_what + " is accepted" + (problem ? ": " + problem->where + ": " + problem->message : ""));
		if (problem) {
			return;
		}
		const auto back = ParseRecipe(SerializeRecipe(a_recipe), a_recipe.id);
		Check(back.recipe && *back.recipe == a_recipe, a_what + ": the edited recipe serialises to a file that reads back identical");
		EditHistory history;
		Recipe      current = a_recipe;
		history.Push(before);
		const auto undone = history.Undo(current);
		Check(undone && *undone == before, a_what + ": undo restores the recipe before the edit exactly");
	}

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
		Refused(r, SetMask{ "metal", std::string(kMaxExpressionLength + 1, '1') }, "mask metal", "longer than 4096 characters", "mask text past the expression length");
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
		Check(Describe(AddSignal{ "a" }) == "signals: add a" && Describe(AddCurve{ "c" }) == "curves: add c", "describe add rows");
		Check(Describe(RenameSignal{ "a", "b" }) == "signal a: rename to b" && Describe(RenameCurve{ "c", "d" }) == "curve c: rename to d", "describe renames");
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

namespace
{
	void RowEdits()
	{
		Recipe r = Canonical();
		Accepted(r, AddSignal{ "fresh" }, "add a signal");
		const auto* fresh = r.FindSignal("fresh");
		Check(fresh && Get<ConstantSignal>(fresh->kind) && Get<float>(Get<ConstantSignal>(fresh->kind)->value) && !fresh->curve, "the added signal is a constant 0 without a curve");
		Refused(r, AddSignal{ "fresh" }, "signal fresh", "a signal has that name", "add a signal twice");
		Refused(r, AddSignal{ "2fresh" }, "signal 2fresh", "letters, digits and underscores", "add a signal with a bad name");
		Refused(r, AddSignal{ "" }, "signal ", "letters, digits and underscores", "add a signal without a name");
		Accepted(r, AddCurve{ "ease" }, "add a curve");
		const auto* ease = r.FindCurve("ease");
		Check(ease && ease->text == "x", "the added curve is x");
		Refused(r, AddCurve{ "ease" }, "curve ease", "a curve has that name", "add a curve twice");
		Refused(r, AddCurve{ "bad name" }, "curve bad name", "letters, digits and underscores", "add a curve with a bad name");

		Recipe hue = Canonical();
		Refused(hue, RenameSignal{ "nothing", "x" }, "signal nothing", "no such signal", "rename a missing signal");
		Refused(hue, RenameSignal{ "glowHue", "glowStrength" }, "signal glowHue", "already named", "rename onto a taken name");
		Refused(hue, RenameSignal{ "glowHue", "1hue" }, "signal glowHue", "letters, digits and underscores", "rename to a bad name");
		Accepted(hue, RenameSignal{ "glowHue", "glowHue" }, "rename to the same name");
		Check(hue == Canonical(), "renaming to the same name changes nothing");
		Accepted(hue, RenameSignal{ "glowHue", "hue" }, "rename glowHue");
		Check(!hue.FindSignal("glowHue") && hue.FindSignal("hue"), "the row carries the new name");
		const auto* fill = LayerAt(hue, 0, 0);
		Check(fill && fill->color && Get<Ref>(*fill->color) && Get<Ref>(*fill->color)->name == "hue", "the fill layer's colour follows the rename");
		const auto* light = std::get_if<LightOutput>(&hue.outputs[4]);
		Check(light && Get<Ref>(light->color) && Get<Ref>(light->color)->name == "hue", "the light's colour follows the rename");
		Check(hue.variants.size() == 1 && hue.variants[0].overrides.contains("hue") && !hue.variants[0].overrides.contains("glowHue"), "the variant override follows the rename");

		Recipe level = Canonical();
		Accepted(level, RenameSignal{ "fillLevel", "fill_level" }, "rename fillLevel");
		const auto* glowLevel = level.FindSignal("glowLevel");
		const auto* lightLevel = level.FindSignal("lightLevel");
		Check(glowLevel && Get<ExprSignal>(glowLevel->kind) && Get<ExprSignal>(glowLevel->kind)->text == "@glowStrength * @fill_level", "an expression follows the rename");
		Check(lightLevel && Get<ExprSignal>(lightLevel->kind) && Get<ExprSignal>(lightLevel->kind)->text == "clamp(@fill_level, 0, 2)", "a reference before a comma follows the rename");
		Accepted(level, RenameSignal{ "struck", "hit" }, "rename a trigger");
		const auto* ring = level.FindSource("ring");
		Check(ring && Get<RippleSource>(ring->kind) && Get<RippleSource>(ring->kind)->trigger.name == "hit", "a ripple's trigger follows the rename");
		const auto* ringLayer = LayerAt(level, 0, 1);
		Check(ringLayer && Get<Ref>(ringLayer->opacity) && Get<Ref>(ringLayer->opacity)->name == "hit", "a layer's opacity follows the rename");
		Accepted(level, RenameSignal{ "scroll", "drift" }, "rename scroll");
		const auto* fillSource = level.FindSource("fill");
		const auto* image = fillSource ? Get<ImageSource>(fillSource->kind) : nullptr;
		Check(image && image->scroll && Get<Ref>(*image->scroll) && Get<Ref>(*image->scroll)->name == "drift", "an image's scroll follows the rename");
		const auto* sheenScroll = level.FindSignal("sheenScroll");
		Check(sheenScroll && Get<ExprSignal>(sheenScroll->kind)->text == "@drift + 0.5", "the expressions reading scroll follow");
		Accepted(level, RenameSignal{ "inflate", "swell" }, "rename inflate");
		const auto* parts = Get<std::array<Param, 3>>(level.shell.pose.inflate);
		Check(parts && Get<Ref>((*parts)[1]) && Get<Ref>((*parts)[1])->name == "swell", "the shell's inflate follows the rename");
		Accepted(level, RenameSignal{ "shellOpacity", "veil" }, "rename shellOpacity");
		Check(Get<Ref>(level.shell.alpha) && Get<Ref>(level.shell.alpha)->name == "veil", "the shell's alpha follows the rename");

		Recipe shared = Canonical();
		shared.signals.push_back(Signal{ "metallic", ConstantSignal{ 1.0f }, std::nullopt });
		Accepted(shared, RenameSignal{ "metallic", "shine" }, "rename a signal an image shares a name with");
		Check(shared.masks[0].text == "@metallic", "the mask still reads the image");

		Recipe curve = Canonical();
		curve.masks[0].text = "@flash(@metallic) + @flash (0.5)";
		Refused(curve, RenameCurve{ "nothing", "x" }, "curve nothing", "no such curve", "rename a missing curve");
		Refused(curve, RenameCurve{ "flash", "rest" }, "curve flash", "already named", "rename a curve onto a taken name");
		Accepted(curve, RenameCurve{ "flash", "blink" }, "rename flash");
		const auto* struck = curve.FindSignal("struck");
		const auto* step = curve.FindSignal("step");
		Check(struck && struck->curve && struck->curve->text == "@blink" && step && step->curve && step->curve->text == "@blink", "the triggers' curves follow the rename");
		Check(curve.masks[0].text == "@blink(@metallic) + @blink (0.5)", "curve calls inside a mask follow the rename");
		Accepted(curve, RenameCurve{ "crisp", "sharp" }, "rename crisp");
		const auto* relief = LayerAt(curve, 2, 0);
		Check(relief && relief->curve && relief->curve->text == "@sharp", "a layer's curve follows the rename");

		Recipe remove = Canonical();
		const auto counts = CountReferences(remove);
		Check(counts.signals.at("glowHue") == 5 && counts.signals.at("scroll") == 4 && counts.signals.at("struck") == 2 && counts.signals.at("glossBoost") == 1 && counts.curves.at("flash") == 2 && counts.curves.at("crisp") == 1 && counts.curves.at("edgeRest") == 1 && !counts.signals.contains("nothing"), "reference counts over parameters, expressions, curves and variants");
		Refused(remove, RemoveSignal{ "glowHue" }, "signal glowHue", "referenced in 5 place(s)", "remove a referenced signal");
		Refused(remove, RemoveSignal{ "nothing" }, "signal nothing", "no such signal", "remove a missing signal");
		Accepted(remove, AddSignal{ "spare" }, "add a spare signal");
		Accepted(remove, RemoveSignal{ "spare" }, "remove an unreferenced signal");
		Check(!remove.FindSignal("spare"), "the signal is gone");
		Check(counts.images.at("metal") == 1 && counts.images.at("fill") == 1 && counts.images.at("metallic") == 1 && counts.images.at("ring") == 1, "image counts: layer sources and masks, and names inside masks");
		Refused(remove, RemoveMask{ "metal" }, "mask metal", "referenced in 1 place(s)", "remove a referenced mask");
		Refused(remove, RemoveMask{ "nothing" }, "mask nothing", "no such mask", "remove a missing mask");
		Refused(remove, AddMask{ "fill" }, "mask fill", "a mask or source has that name", "a mask named like a source");
		Accepted(remove, AddMask{ "straps" }, "add a mask");
		Check(remove.FindMask("straps") && remove.FindMask("straps")->text == "1", "the added mask reads 1");
		Accepted(remove, RemoveMask{ "straps" }, "remove an unreferenced mask");
		Check(!remove.FindMask("straps"), "the mask is gone");
		Recipe renamed = Canonical();
		renamed.masks.push_back(Mask{ "metalToo", "@metal * 0.5" });
		Refused(renamed, RenameMask{ "metal", "fill" }, "mask metal", "already named", "rename a mask onto a source's name");
		Accepted(renamed, RenameMask{ "metal", "steel" }, "rename a mask");
		const auto* fillLayer = LayerAt(renamed, 0, 0);
		Check(fillLayer && fillLayer->mask && fillLayer->mask->name == "steel", "the layer's mask follows the rename");
		Check(renamed.masks[1].text == "@steel * 0.5" && renamed.FindMask("steel"), "another mask's expression follows the rename");
		Refused(remove, RemoveSource{ "fill" }, "source fill", "referenced in 1 place(s)", "remove a referenced source");
		Refused(remove, AddSource{ "metal", ImageSource{} }, "source metal", "a source or mask has that name", "a source named like a mask");
		Accepted(remove, AddSource{ "noise", ImageSource{} }, "add an image without a path yet");
		Refused(remove, SetSource{ "noise", ImageSource{} }, "source noise", "'path' is empty", "set an image without a path");
		Refused(remove, SetSource{ "noise", RippleSource{ Ref{ "nothing" } } }, "source noise", "unknown signal", "a ripple reading a missing trigger");
		Refused(remove, SetSource{ "noise", BakeSource{ BoneWeightBake{} } }, "source noise", "at least one bone", "a boneWeight bake without bones");
		ImageSource noiseImage;
		noiseImage.path = "Effects\\Noise.dds";
		Accepted(remove, SetSource{ "noise", noiseImage }, "set the image's path");
		Check(remove.FindSource("noise") && Get<ImageSource>(remove.FindSource("noise")->kind) && Get<ImageSource>(remove.FindSource("noise")->kind)->path == "Effects\\Noise.dds", "the source carries the kind");
		Refused(remove, RenameSource{ "noise", "metal" }, "source noise", "already named", "rename a source onto a mask's name");
		Accepted(remove, RenameSource{ "fill", "swirls" }, "rename the fill source");
		Check(LayerAt(remove, 0, 0) && Get<Ref>(LayerAt(remove, 0, 0)->source) && Get<Ref>(LayerAt(remove, 0, 0)->source)->name == "swirls", "the layer's source follows the rename");
		Accepted(remove, RenameSource{ "metallic", "shine" }, "rename the metallic source");
		Check(remove.masks[0].text == "@shine", "the mask's expression follows the rename");
		Accepted(remove, RemoveSource{ "noise" }, "remove an unreferenced source");
		Check(!remove.FindSource("noise"), "the source is gone");
		Refused(remove, RemoveCurve{ "flash" }, "curve flash", "referenced in 2 place(s)", "remove a referenced curve");
		Accepted(remove, AddCurve{ "spareCurve" }, "add a spare curve");
		Accepted(remove, RemoveCurve{ "spareCurve" }, "remove an unreferenced curve");
		Check(!remove.FindCurve("spareCurve"), "the curve is gone");

		Check(RenameInExpression("@a + @ab + @a(1) + @a", "a", "z", false) == "@z + @ab + @a(1) + @z", "a signal rename leaves longer names and curve calls");
		Check(RenameInExpression("@a + @a(1) + @a (2)", "a", "z", true) == "@a + @z(1) + @z (2)", "a curve rename takes only calls");
		Check(RenameInExpression("", "a", "z", false).empty() && RenameInExpression("@", "a", "z", false) == "@", "empty and bare texts pass through");
	}
}

namespace
{
	void PanelEdits()
	{
		Recipe r = Canonical();
		Refused(r, AddLight{}, "outputs", "output 4 is already the light", "add a second light");
		Refused(r, SetLightParam{ 0, LightParam::kIntensity, 1.0f }, "output 0", "not a light", "a light edit on a material output");
		Refused(r, SetLightParam{ 9, LightParam::kIntensity, 1.0f }, "output 9", "5 outputs", "a light edit past the end");
		Refused(r, SetLightParam{ 4, LightParam::kIntensity, Ref{ "nothing" } }, "output 4", "unknown signal", "a light parameter reading a missing signal");
		Accepted(r, SetLightParam{ 4, LightParam::kSize, 2.0f }, "set the light's size");
		Accepted(r, SetLightParam{ 4, LightParam::kCutoff, Ref{ "glowLevel" } }, "set the light's cutoff to a signal");
		Accepted(r, SetLightVector{ 4, LightVector::kColor, Ref{ "edgeColor" } }, "set the light's colour to a signal");
		Accepted(r, SetLightVector{ 4, LightVector::kOffset, std::array<Param, 3>{ 0.0f, 0.0f, 5.0f } }, "set the light's offset");
		Accepted(r, SetLightShadow{ 4, true }, "set the light's shadow");
		Refused(r, SetLightVector{ 9, LightVector::kColor, std::array<Param, 3>{ 1.0f, 1.0f, 1.0f } }, "output 9", "there are", "a light vector on an output past the end");
		Refused(r, SetLightVector{ 0, LightVector::kColor, std::array<Param, 3>{ 1.0f, 1.0f, 1.0f } }, "output 0", "not a light", "a light vector on a material output");
		Refused(r, SetLightVector{ 4, LightVector::kColor, Ref{ "nobody" } }, "output 4", "nobody", "a light vector naming a signal the recipe lacks");
		Refused(r, SetLightShadow{ 9, true }, "output 9", "there are", "a shadow on an output past the end");
		Refused(r, SetLightShadow{ 0, true }, "output 0", "not a light", "a shadow on a material output");
		Accepted(r, SetLightBones{ 4, NamedBones{ { "NPC Head [Head]" } } }, "set named bones");
		Refused(r, SetLightBones{ 4, NamedBones{} }, "output 4", "at least one name", "named bones without names");
		Refused(r, SetLightBones{ 4, SkinnedBones{ 0, 0.0f } }, "output 4", "at least 1", "skinned bones with max 0");
		const auto* light = std::get_if<LightOutput>(&r.outputs[4]);
		Check(light && Get<float>(light->size) && *Get<float>(light->size) == 2.0f && Get<Ref>(light->cutoff) && Get<Ref>(light->color) && light->shadow && Get<NamedBones>(light->bones), "the light carries every edit");
		Refused(r, ResetLight{ 0 }, "output 0", "not a light", "reset a material output as a light");
		Accepted(r, ResetLight{ 4 }, "reset the light");
		Check(light && *light == LightOutput{}, "the reset light has the format's defaults");
		Accepted(r, RemoveOutput{ 4 }, "remove the light");
		Accepted(r, AddLight{}, "add a light back");
		Check(r.outputs.size() == 5 && std::get_if<LightOutput>(&r.outputs[4]) && *std::get_if<LightOutput>(&r.outputs[4]) == LightOutput{}, "the added light has the format's defaults");

		Recipe s = Canonical();
		Refused(s, SetShellMaterial{ ShellMaterial::kVanilla }, "shell", "writes 'fuzz' on the shell", "a vanilla shell under a fuzz output");
		Accepted(s, RemoveOutput{ 1 }, "remove the fuzz output");
		Accepted(s, SetShellMaterial{ ShellMaterial::kVanilla }, "a vanilla shell under emissive alone");
		Accepted(s, SetShellBlend{ ShellBlend::kAlpha }, "set the blend");
		Accepted(s, SetShellDepthBias{ false }, "set the depth bias");
		Accepted(s, SetShellAlphaTest{ 0.5f }, "set the alpha test");
		Refused(s, SetShellAlphaTest{ 1.5f }, "shell", "0..1", "an alpha test out of range");
		Accepted(s, SetShellParam{ ShellParam::kRimPower, 4.0f }, "set the rim power");
		Refused(s, SetShellParam{ ShellParam::kEmissive, Ref{ "nothing" } }, "shell", "unknown signal", "a shell parameter reading a missing signal");
		Accepted(s, SetShellVector{ ShellVector::kOffset, Ref{ "glowHue" } }, "set the pose offset to a signal");
		Refused(s, SetShellVector{ ShellVector::kOffset, Ref{ "nobody" } }, "shell", "nobody", "a shell vector naming a signal the recipe lacks");
		Accepted(s, SetShellPoint{ ShellPoint::kScalePoint, Vec3{ 1.0f, 2.0f, 3.0f } }, "set the scale point");
		Refused(s, SetShellPoint{ ShellPoint::kSpinAxis, Vec3{} }, "shell", "cannot be zero", "a zero spin axis");
		Check(s.shell.material == ShellMaterial::kVanilla && s.shell.blend == ShellBlend::kAlpha && !s.shell.depthBias && s.shell.alphaTest == 0.5f && Get<float>(s.shell.rimPower) && Get<Ref>(s.shell.pose.offset) && s.shell.pose.scalePoint == Vec3{ 1.0f, 2.0f, 3.0f }, "the shell carries every edit");
		Accepted(s, ResetShell{}, "reset the shell");
		Check(s.shell == ShellSettings{}, "the reset shell has the format's defaults");
		Check(Describe(ResetShell{}) == "shell: reset" && Describe(ResetLight{ 4 }) == "output 4: reset light", "describe the resets");
	}

	void KeyEdits()
	{
		Recipe r = Canonical();
		Check(r.keys.size() == 1, "the canonical recipe has one key");
		const RecipeKey first = r.keys[0];
		Refused(r, RemoveKey{ first }, "key " + first.ToString(), "at least one key", "remove the last key");
		RecipeKey armor;
		armor.kind = KeyKind::kArmor;
		armor.form = FormRef::From("0x12E49~Skyrim.esm");
		Accepted(r, AddKey{ armor }, "add an armor key");
		Check(r.keys.size() == 2 && r.keys[1] == armor, "the armor key is appended");
		Refused(r, AddKey{ armor }, "key " + armor.ToString(), "has that key", "add a key twice");
		RecipeKey blank;
		blank.kind = KeyKind::kKeyword;
		Refused(r, AddKey{ blank }, "key " + blank.ToString(), "names a form", "add a keyword key with no form");
		RecipeKey material;
		material.kind = KeyKind::kMaterial;
		Refused(r, AddKey{ material }, "key " + material.ToString(), "needs a glob", "add a material key with no glob");
		Accepted(r, RemoveKey{ armor }, "remove the armor key");
		Check(r.keys.size() == 1 && r.keys[0] == first, "the first key remains");
		Refused(r, RemoveKey{ armor }, "key " + armor.ToString(), "no such key", "remove a key the recipe lacks");

		Recipe s;
		s.id = "fresh";
		s.keys.push_back(first);
		Selector torso;
		torso.anyOf.push_back(SelectorClause{ SelectorKind::kGeometry, {}, "torso" });
		Accepted(s, AddOutput{ Surface::kMaterial, Slot::kEmissive, torso }, "add an output selecting the torso");
		Accepted(s, AddOutput{ Surface::kMaterial, Slot::kRmaos }, "add an output with no selector");
		const auto* second = Get<SurfaceOutput>(s.outputs[1]);
		Check(second && second->selector == torso, "the second output inherits the selector every output shares");
		Selector hands;
		hands.anyOf.push_back(SelectorClause{ SelectorKind::kGeometry, {}, "hands" });
		Accepted(s, AddOutput{ Surface::kMaterial, Slot::kHeight, hands }, "add an output selecting the hands");
		Accepted(s, AddOutput{ Surface::kShell, Slot::kEmissive }, "add an output while the selectors differ");
		const auto* fourth = Get<SurfaceOutput>(s.outputs[3]);
		Check(fourth && fourth->selector.All(), "with differing selectors a new output selects every geometry");
	}

	void ClearEdits()
	{
		Recipe outputs = Canonical();
		Accepted(outputs, ClearOutputs{}, "clear the outputs");
		Check(outputs.outputs.empty() && outputs.shell == ShellSettings{} && outputs.keys == Canonical().keys && outputs.signals.size() == Canonical().signals.size(), "clearing outputs leaves keys and resources");

		Recipe whole = Canonical();
		Accepted(whole, ClearRecipe{}, "clear the recipe");
		Check(whole.id == Canonical().id && whole.metadata.name == Canonical().metadata.name && whole.keys == Canonical().keys, "clearing the recipe keeps its id, name and keys");
		Check(whole.outputs.empty() && whole.shell == ShellSettings{} && whole.signals.empty() && whole.curves.empty() && whole.sources.empty() && whole.masks.empty() && whole.variants.empty(), "clearing the recipe empties everything else");
		Check(Describe(ClearRecipe{}) == "recipe: clear", "describe clear recipe");

		Recipe resources = Canonical();
		Accepted(resources, ClearResources{}, "clear the resources");
		Check(resources.signals.empty() && resources.curves.empty() && resources.sources.empty() && resources.masks.empty() && resources.variants.empty(), "every resource is gone");
		const auto* emissive = Get<SurfaceOutput>(resources.outputs[0]);
		const auto* fuzz = Get<SurfaceOutput>(resources.outputs[1]);
		Check(emissive && emissive->stack.empty() && fuzz && fuzz->stack.size() == 1 && Is<Vec3>(fuzz->stack[0].source) && !fuzz->stack[0].mask && !fuzz->stack[0].curve, "layers on a source go; a constant layer stays without mask or curve");
		Check(emissive && emissive->scalars.strength == Param{ 1.0f } && fuzz && fuzz->scalars.weight == Param{ 1.0f } && fuzz->scalars.color == Vec3Param{ std::array<Param, 3>{ 1.0f, 1.0f, 1.0f } }, "required scalars that named a signal return to their fallbacks");
		const auto* light = Get<LightOutput>(resources.outputs[4]);
		Check(light && Is<std::array<Param, 3>>(light->color) && Is<float>(light->intensity), "the light's colour and intensity are literal again");
		Check(Is<float>(resources.shell.alpha) && Is<std::array<Param, 3>>(resources.shell.pose.inflate), "the shell's alpha and inflation are literal again");
		const auto problems = Validate(resources);
		std::string errors;
		for (const auto& d : problems) {
			if (d.severity == Severity::kError) {
				errors += d.where + ": " + d.message + "; ";
			}
		}
		Check(errors.empty(), "a cleared recipe validates without errors: " + errors);
	}

void BatchEdits()
{
	Recipe r = Canonical();
	const Recipe before = r;
	const auto   refused = Apply(r, EditBatch{ { AddSignal{ "batched" }, SetConstant{ "nobody", 1.0f } } });
	Check(refused && refused->where == "signal nobody" && r == before, "a batch with a refused edit is refused whole and leaves the recipe as it was");
	const auto accepted = Apply(r, EditBatch{ { AddSignal{ "batched" }, SetConstant{ "batched", 2.0f } } });
	const auto* made = r.FindSignal("batched");
	const auto* constant = made ? Get<ConstantSignal>(made->kind) : nullptr;
	Check(!accepted && constant && constant->value == Value{ 2.0f }, "an accepted batch applies every edit in order");
	Check(Describe(EditBatch{ { AddSignal{ "a" }, RemoveSignal{ "a" } } }) == "signals: add a; signal a: remove", "a batch describes as its edits joined");
	Check(ChangesKeys(EditBatch{ { AddKey{ RecipeKey{ KeyKind::kDefault, {}, {} } } } }) && !ChangesKeys(EditBatch{ { AddSignal{ "a" } } }), "a batch knows whether it changes keys");
	Check(!Apply(r, EditBatch{}) && r.FindSignal("batched"), "an empty batch is accepted and changes nothing");
}
}

int main()
{
	Check(!Canonical().outputs.empty(), "schema/example-magicka.json parses");
	RowEdits();
	PanelEdits();
	LayerEdits();
	StackEdits();
	OutputEdits();
	KeyEdits();
	ClearEdits();
	ScalarEdits();
	SignalEdits();
	Serialised();
	Described();
	BatchEdits();
	return test::Finish("edits");
}
