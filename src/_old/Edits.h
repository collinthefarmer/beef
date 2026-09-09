#pragma once

#include "Core.h"
#include "Recipe.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects::Studio
{

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
	struct MoveLayer
	{
		std::size_t output = 0;
		std::size_t from = 0;
		std::size_t to = 0;
	};
	struct ClearLayers
	{
		std::size_t output = 0;
	};

	struct AddOutput
	{
		Surface  surface = Surface::kMaterial;
		Slot     slot = Slot::kEmissive;
		Selector selector;
	};
	struct AddKey
	{
		RecipeKey key;
	};
	struct RemoveKey
	{
		RecipeKey key;
	};
	struct ClearOutputs
	{
	};
	struct ClearResources
	{
	};
	struct ClearRecipe
	{
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
	struct SetSignal
	{
		std::string signal;
		SignalKind  kind = ConstantSignal{};
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
	struct AddSignal
	{
		std::string name;
	};
	struct AddCurve
	{
		std::string name;
	};
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
	struct RemoveSignal
	{
		std::string name;
	};
	struct RemoveCurve
	{
		std::string name;
	};
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
	struct ResetLight
	{
		std::size_t output = 0;
	};

	enum class ShellParam
	{
		kAlpha,
		kRimPower,
		kEmissive,
		kScale,
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
		float value = 0.0f;
	};
	struct ResetShell
	{
	};

	using RecipeEdit = std::variant<
		SetLayerSource, SetLayerCurve, SetLayerBlend, SetLayerOpacity, SetLayerColor, SetLayerMask, SetLayerChannels,
		AddLayer, RemoveLayer, MoveLayer, ClearLayers,
		AddOutput, RemoveOutput, SetScalar, SetColorScalar, AddKey, RemoveKey, ClearOutputs, ClearResources, ClearRecipe,
		SetConstant, SetExpression, SetSignal, SetSignalCurve, SetCurve, SetMask,
		AddSignal, AddCurve, RenameSignal, RenameCurve, RemoveSignal, RemoveCurve, AddMask, RenameMask, RemoveMask,
		AddSource, SetSource, RenameSource, RemoveSource,
		AddLight, SetLightParam, SetLightVector, SetLightShadow, SetLightBones, ResetLight,
		SetShellParam, SetShellVector, SetShellPoint, SetShellMaterial, SetShellBlend, SetShellDepthBias, SetShellAlphaTest, ResetShell>;

	[[nodiscard]] std::optional<Diagnostic> Apply(Recipe& a_recipe, const RecipeEdit& a_edit);

	[[nodiscard]] std::string Describe(const RecipeEdit& a_edit);

	struct EditBatch
	{
		std::vector<RecipeEdit> edits;
	};
	[[nodiscard]] std::optional<Diagnostic> Apply(Recipe& a_recipe, const EditBatch& a_batch);
	[[nodiscard]] std::string               Describe(const EditBatch& a_batch);

	struct ReferenceCounts
	{
		std::map<std::string, std::size_t> signals;
		std::map<std::string, std::size_t> curves;
		std::map<std::string, std::size_t> images;
	};
	[[nodiscard]] ReferenceCounts CountReferences(const Recipe& a_recipe);

	[[nodiscard]] std::string RenameInExpression(std::string_view a_text, std::string_view a_from, std::string_view a_to, bool a_curve);

	[[nodiscard]] Layer DefaultLayer();
	[[nodiscard]] SurfaceOutput DefaultOutput(Surface a_surface, Slot a_slot);
}
