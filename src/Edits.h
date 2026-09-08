#pragma once

// Every change the menu makes to a recipe, as data. The page builds one of
// these from what a widget returned and hands it to Manager::EditRecipe,
// which applies it on the game thread with the wearers retired. Apply is a
// pure function over the recipe, so an edit is tested by applying it to the
// canonical recipe and comparing the file it serialises to.

#include "Core.h"
#include "Recipe.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace WornEnchantmentPBR::Studio
{
	// ----------------------------------------------------------------- layers

	struct SetLayerSource
	{
		std::size_t output = 0;
		std::size_t layer = 0;
		LayerSource source;
	};
	struct SetLayerCurve
	{
		std::size_t             output = 0;
		std::size_t             layer = 0;
		std::optional<CurveRef> curve;
	};
	struct SetLayerBlend
	{
		std::size_t output = 0;
		std::size_t layer = 0;
		Blend       blend = Blend::kReplace;
	};
	struct SetLayerOpacity
	{
		std::size_t output = 0;
		std::size_t layer = 0;
		Param       opacity = 1.0f;
	};
	struct SetLayerColor
	{
		std::size_t              output = 0;
		std::size_t              layer = 0;
		std::optional<Vec3Param> color;
	};
	struct SetLayerMask
	{
		std::size_t        output = 0;
		std::size_t        layer = 0;
		std::optional<Ref> mask;
	};
	struct SetLayerChannels
	{
		std::size_t output = 0;
		std::size_t layer = 0;
		ChannelSet  channels;
	};
	// `at` is the index the new layer takes; absent = on top (the end).
	struct AddLayer
	{
		std::size_t                output = 0;
		Layer                      layer;
		std::optional<std::size_t> at;
	};
	struct RemoveLayer
	{
		std::size_t output = 0;
		std::size_t layer = 0;
	};
	// The layer at `from` ends up at index `to` (the index it has after the move).
	struct MoveLayer
	{
		std::size_t output = 0;
		std::size_t from = 0;
		std::size_t to = 0;
	};
	// Every layer of the output removed; the stack is left empty.
	struct ClearLayers
	{
		std::size_t output = 0;
	};

	// ---------------------------------------------------------------- outputs

	// An empty stack on a slot, with the slot's required scalars at their
	// defaults (DefaultOutput). Refused when the surface lacks the slot or
	// another output of the recipe excludes it.
	struct AddOutput
	{
		Surface  surface = Surface::kMaterial;
		Slot     slot = Slot::kEmissive;
		Selector selector;  // empty: the selector every existing output shares, else every geometry
	};
	// Keys: which pieces the recipe resolves to. An added key must not
	// repeat one the recipe has; the last key is never removed.
	struct AddKey
	{
		RecipeKey key;
	};
	struct RemoveKey
	{
		RecipeKey key;
	};
	struct RemoveOutput
	{
		std::size_t output = 0;
	};
	struct SetScalar
	{
		std::size_t output = 0;
		ScalarField field = ScalarField::kStrength;
		Param       value = 1.0f;
	};
	struct SetColorScalar
	{
		std::size_t output = 0;
		Vec3Param   color = std::array<Param, 3>{ 1.0f, 1.0f, 1.0f };
	};

	// ----------------------------------------------------------------- signals

	struct SetConstant
	{
		std::string signal;
		Value       value = 0.0f;
	};
	struct SetExpression
	{
		std::string signal;
		std::string text;
	};
	struct SetSignalCurve
	{
		std::string             signal;
		std::optional<CurveRef> curve;
	};
	struct SetCurve
	{
		std::string curve;
		std::string text;
	};
	struct SetMask
	{
		std::string mask;
		std::string text;
	};
	// A constant 0 under the name; refused when the name is taken or is not
	// a row name.
	struct AddSignal
	{
		std::string name;
	};
	// The curve "x" under the name.
	struct AddCurve
	{
		std::string name;
	};
	// The row renamed and every reference to it repointed: `@from` in a
	// parameter, a trigger reference, a variant's override, and inside every
	// expression (signals, masks, curves, inline curves), where a signal is
	// `@name` and a curve is `@name(`. A mask expression is left alone when
	// a source or mask also carries the old name, since there the image wins.
	struct RenameSignal
	{
		std::string from;
		std::string to;
	};
	struct RenameCurve
	{
		std::string from;
		std::string to;
	};
	// A row removed; refused while anything references it (CountReferences).
	struct RemoveSignal
	{
		std::string name;
	};
	struct RemoveCurve
	{
		std::string name;
	};
	// Masks as rows: a new one reads "1" (the whole piece); a rename
	// repoints the layers' masks and sources and `@name` inside masks; a
	// removal is refused while referenced.
	struct AddMask
	{
		std::string name;
	};
	struct RenameMask
	{
		std::string from;
		std::string to;
	};
	struct RemoveMask
	{
		std::string name;
	};
	// Sources as rows: a new one of a kind at its defaults; the kind set
	// whole (a form rebuilds it from its texts); a rename repoints the
	// layers' sources and `@name` inside masks; a removal is refused while
	// referenced.
	struct AddSource
	{
		std::string name;
		SourceKind  kind = MaterialSource{};
	};
	struct SetSource
	{
		std::string name;
		SourceKind  kind = MaterialSource{};
	};
	struct RenameSource
	{
		std::string from;
		std::string to;
	};
	struct RemoveSource
	{
		std::string name;
	};

	// ------------------------------------------------------------- light

	// A light output with the format's defaults; refused when the recipe
	// already has one.
	struct AddLight
	{
	};
	enum class LightParam
	{
		kIntensity,
		kSize,
		kCutoff,
	};
	enum class LightVector
	{
		kColor,
		kOffset,
	};
	[[nodiscard]] std::string_view LightParamName(LightParam a_field) noexcept;
	[[nodiscard]] std::string_view LightVectorName(LightVector a_field) noexcept;
	// Each names the light output by index and is refused when that output
	// is not a light.
	struct SetLightParam
	{
		std::size_t output = 0;
		LightParam  field = LightParam::kIntensity;
		Param       value = 1.0f;
	};
	struct SetLightVector
	{
		std::size_t output = 0;
		LightVector field = LightVector::kColor;
		Vec3Param   value = std::array<Param, 3>{ 1.0f, 1.0f, 1.0f };
	};
	struct SetLightShadow
	{
		std::size_t output = 0;
		bool        shadow = false;
	};
	struct SetLightBones
	{
		std::size_t output = 0;
		Bones       bones = SkinnedBones{};
	};
	// The light's settings back to the format's defaults; its selector and
	// replace stay, since they place the output, not the light.
	struct ResetLight
	{
		std::size_t output = 0;
	};

	// ------------------------------------------------------------- shell

	enum class ShellParam
	{
		kAlpha,
		kRimPower,
		kEmissive,
		kScale,  // pose
		kSpin,
	};
	enum class ShellVector
	{
		kInflate,
		kOffset,
	};
	enum class ShellPoint
	{
		kScalePoint,
		kSpinAxis,
	};
	[[nodiscard]] std::string_view ShellParamName(ShellParam a_field) noexcept;
	[[nodiscard]] std::string_view ShellVectorName(ShellVector a_field) noexcept;
	[[nodiscard]] std::string_view ShellPointName(ShellPoint a_field) noexcept;
	struct SetShellParam
	{
		ShellParam field = ShellParam::kAlpha;
		Param      value = 1.0f;
	};
	struct SetShellVector
	{
		ShellVector field = ShellVector::kInflate;
		Vec3Param   value = std::array<Param, 3>{ 0.0f, 0.0f, 0.0f };
	};
	struct SetShellPoint
	{
		ShellPoint field = ShellPoint::kScalePoint;
		Vec3       value;
	};
	// Changing the material kind changes the slots the shell offers; a
	// shell output on a slot the new kind lacks is refused.
	struct SetShellMaterial
	{
		ShellMaterial material = ShellMaterial::kPbrCopy;
	};
	struct SetShellBlend
	{
		ShellBlend blend = ShellBlend::kAdditive;
	};
	struct SetShellDepthBias
	{
		bool on = true;
	};
	struct SetShellAlphaTest
	{
		float value = 0.0f;  // 0..1; 0 is off
	};
	// Every shell setting back to the format's defaults (a PBR copy offers
	// every slot, so no shell output is stranded).
	struct ResetShell
	{
	};

	using RecipeEdit = std::variant<
		SetLayerSource, SetLayerCurve, SetLayerBlend, SetLayerOpacity, SetLayerColor, SetLayerMask, SetLayerChannels,
		AddLayer, RemoveLayer, MoveLayer, ClearLayers,
		AddOutput, RemoveOutput, SetScalar, SetColorScalar, AddKey, RemoveKey,
		SetConstant, SetExpression, SetSignalCurve, SetCurve, SetMask,
		AddSignal, AddCurve, RenameSignal, RenameCurve, RemoveSignal, RemoveCurve, AddMask, RenameMask, RemoveMask,
		AddSource, SetSource, RenameSource, RemoveSource,
		AddLight, SetLightParam, SetLightVector, SetLightShadow, SetLightBones, ResetLight,
		SetShellParam, SetShellVector, SetShellPoint, SetShellMaterial, SetShellBlend, SetShellDepthBias, SetShellAlphaTest, ResetShell>;

	// Applies one edit. The recipe is unchanged when the edit does not fit
	// it (an index past the end, a name the recipe lacks, a slot the surface
	// lacks, a scalar the slot does not carry) and the diagnostic says why,
	// with the row in `where` ("output 2 layer 0", "signal glowHue").
	[[nodiscard]] std::optional<Diagnostic> Apply(Recipe& a_recipe, const RecipeEdit& a_edit);

	// One line for the log: "output 2 layer 0: blend add".
	[[nodiscard]] std::string Describe(const RecipeEdit& a_edit);

	// How many places name each signal and each curve: a signal in a
	// parameter, a trigger reference, a variant override, or `@name` inside
	// an expression; a curve as a signal's or layer's `@name`, or `@name(`
	// inside an expression. A name absent from the map is unreferenced.
	struct ReferenceCounts
	{
		std::map<std::string, std::size_t> signals;
		std::map<std::string, std::size_t> curves;
		std::map<std::string, std::size_t> images;  // sources and masks: layer sources and masks, `@name` inside masks
	};
	[[nodiscard]] ReferenceCounts CountReferences(const Recipe& a_recipe);

	// The text with every `@from` reference renamed to `@to`: a signal
	// reference is the name followed by anything but a name character or
	// '('; a curve reference is the name followed by '('.
	[[nodiscard]] std::string RenameInExpression(std::string_view a_text, std::string_view a_from, std::string_view a_to, bool a_curve);

	// A white replace layer at full opacity: what Add layer makes.
	[[nodiscard]] Layer DefaultLayer();
	// An empty stack on the slot with its required scalars at their defaults.
	[[nodiscard]] SurfaceOutput DefaultOutput(Surface a_surface, Slot a_slot);
}
