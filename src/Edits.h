#pragma once

// Every change the menu makes to a recipe, as data. The page builds one of
// these from what a widget returned and hands it to Manager::EditRecipe,
// which applies it on the game thread with the wearers retired. Apply is a
// pure function over the recipe, so an edit is tested by applying it to the
// canonical recipe and comparing the file it serialises to.

#include "Core.h"
#include "Recipe.h"

#include <cstddef>
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

	// ---------------------------------------------------------------- outputs

	// An empty stack on a slot, with the slot's required scalars at their
	// defaults (DefaultOutput). Refused when the surface lacks the slot or
	// another output of the recipe excludes it.
	struct AddOutput
	{
		Surface surface = Surface::kMaterial;
		Slot    slot = Slot::kEmissive;
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

	using RecipeEdit = std::variant<
		SetLayerSource, SetLayerCurve, SetLayerBlend, SetLayerOpacity, SetLayerColor, SetLayerMask, SetLayerChannels,
		AddLayer, RemoveLayer, MoveLayer,
		AddOutput, RemoveOutput, SetScalar, SetColorScalar,
		SetConstant, SetExpression, SetSignalCurve, SetCurve, SetMask>;

	// Applies one edit. The recipe is unchanged when the edit does not fit
	// it (an index past the end, a name the recipe lacks, a slot the surface
	// lacks, a scalar the slot does not carry) and the diagnostic says why,
	// with the row in `where` ("output 2 layer 0", "signal glowHue").
	[[nodiscard]] std::optional<Diagnostic> Apply(Recipe& a_recipe, const RecipeEdit& a_edit);

	// One line for the log: "output 2 layer 0: blend add".
	[[nodiscard]] std::string Describe(const RecipeEdit& a_edit);

	// A white replace layer at full opacity: what Add layer makes.
	[[nodiscard]] Layer DefaultLayer();
	// An empty stack on the slot with its required scalars at their defaults.
	[[nodiscard]] MaterialOutput DefaultOutput(Surface a_surface, Slot a_slot);
}
