#pragma once

// The words the recipe format spells: one table per vocabulary with the
// word beside its value, in enum order. The parser reads a field as one of
// a table's words, the writer spells the word back, the name functions in
// Recipe.h look words up here, and a studio choice field lists a table's
// words. A vocabulary lives here and nowhere else; one that carries a rule
// beyond its word (a slot's channels, a key kind's operand) grows a column.

#include "Core.h"
#include "Recipe.h"

#include <iterator>

namespace WornEnchantmentPBR
{
	// A "default" key is written as the bare word, every other kind as
	// {"<kind>": <form or glob>}; the parser refuses "default" as an object.
	inline constexpr Named<KeyKind> kKeyKinds[]{ { KeyKind::kDefault, "default" }, { KeyKind::kMaterial, "material" }, { KeyKind::kKeyword, "keyword" }, { KeyKind::kArmor, "armor" }, { KeyKind::kEffectShader, "effectShader" }, { KeyKind::kEnchantment, "enchantment" }, { KeyKind::kMagicEffect, "magicEffect" } };
	inline constexpr Named<SelectorKind> kSelectorKinds[]{ { SelectorKind::kAddon, "addon" }, { SelectorKind::kGeometry, "geometry" }, { SelectorKind::kTexture, "texture" } };

	inline constexpr Named<Waveform> kWaveforms[]{ { Waveform::kSine, "sine" }, { Waveform::kTriangle, "triangle" }, { Waveform::kSquare, "square" }, { Waveform::kSaw, "saw" } };
	inline constexpr Named<EfshField> kEfshFields[]{ { EfshField::kFillAlpha, "fillAlpha" }, { EfshField::kFillColor, "fillColor" }, { EfshField::kEdgeAlpha, "edgeAlpha" }, { EfshField::kEdgeColor, "edgeColor" }, { EfshField::kScroll, "scroll" } };
	inline constexpr Named<Measure> kMeasures[]{ { Measure::kCurrent, "current" }, { Measure::kBase, "base" }, { Measure::kPermanent, "permanent" }, { Measure::kTemporaryModifier, "temporaryModifier" }, { Measure::kDamage, "damage" }, { Measure::kMax, "max" } };
	inline constexpr Named<ActorStateKind> kActorStates[]{ { ActorStateKind::kInCombat, "inCombat" }, { ActorStateKind::kSneaking, "sneaking" }, { ActorStateKind::kWeaponDrawn, "weaponDrawn" }, { ActorStateKind::kHostileDistance, "hostileDistance" } };
	inline constexpr Named<EnchantmentField> kEnchantmentFields[]{ { EnchantmentField::kMagnitude, "magnitude" }, { EnchantmentField::kCost, "cost" } };
	inline constexpr Named<PayloadField> kPayloadFields[]{ { PayloadField::kValue, "value" }, { PayloadField::kPosition, "position" }, { PayloadField::kNormal, "normal" } };

	inline constexpr Named<ImageChannel> kImageChannels[]{ { ImageChannel::kRgb, "rgb" }, { ImageChannel::kR, "r" }, { ImageChannel::kG, "g" }, { ImageChannel::kB, "b" }, { ImageChannel::kA, "a" }, { ImageChannel::kLuma, "luma" } };
	inline constexpr Named<ImageSpace> kImageSpaces[]{ { ImageSpace::kTiled, "tiled" }, { ImageSpace::kMesh, "mesh" } };
	inline constexpr Named<MaterialChannel> kMaterialChannels[]{ { MaterialChannel::kDiffuseRgb, "diffuseRgb" }, { MaterialChannel::kDiffuseLuma, "diffuseLuma" }, { MaterialChannel::kNormalSlope, "normalSlope" }, { MaterialChannel::kRoughness, "roughness" }, { MaterialChannel::kMetallic, "metallic" }, { MaterialChannel::kOcclusion, "occlusion" }, { MaterialChannel::kReflectance, "reflectance" }, { MaterialChannel::kDisplacement, "displacement" }, { MaterialChannel::kRelief, "relief" } };
	inline constexpr Named<UvAxis> kUvAxes[]{ { UvAxis::kU, "u" }, { UvAxis::kV, "v" } };
	inline constexpr Named<RippleShape> kRippleShapes[]{ { RippleShape::kRing, "ring" }, { RippleShape::kDisc, "disc" } };

	inline constexpr Named<Surface> kSurfaces[]{ { Surface::kMaterial, "material" }, { Surface::kShell, "shell" } };
	// Glint's fallbacks are the values CS starts a material at; the rest are
	// what a fresh output should look like in the studio.
	inline constexpr ScalarFieldRow kScalarFields[]{
		{ ScalarField::kStrength, "strength", &SlotScalars::strength, 1.0f },
		{ ScalarField::kScale, "scale", &SlotScalars::scale, 1.0f },
		{ ScalarField::kColor, "color", &SlotScalars::color, 1.0f },
		{ ScalarField::kWeight, "weight", &SlotScalars::weight, 1.0f },
		{ ScalarField::kScreenSpaceScale, "screenSpaceScale", &SlotScalars::screenSpaceScale, 1.5f },
		{ ScalarField::kLogMicrofacetDensity, "logMicrofacetDensity", &SlotScalars::logMicrofacetDensity, 40.0f },
		{ ScalarField::kMicrofacetRoughness, "microfacetRoughness", &SlotScalars::microfacetRoughness, 0.015f },
		{ ScalarField::kDensityRandomization, "densityRandomization", &SlotScalars::densityRandomization, 2.0f },
		{ ScalarField::kRoughness, "roughness", &SlotScalars::roughness, 0.15f },
		{ ScalarField::kLevel, "level", &SlotScalars::level, 0.6f },
		{ ScalarField::kThickness, "thickness", &SlotScalars::thickness, 1.0f },
	};
	static_assert(std::size(kScalarFields) == kScalarFieldCount);

	namespace SlotColumns
	{
		inline constexpr ChannelSet kRgba{ true, true, true, true };
		inline constexpr ChannelSet kRgb{ true, true, true, false };
		inline constexpr ChannelSet kRedOnly{ true, false, false, false };
		inline constexpr ChannelSet kNoMap{ false, false, false, false };

		inline constexpr ScalarField kEmissiveScalars[]{ ScalarField::kStrength };
		inline constexpr ScalarField kHeightScalars[]{ ScalarField::kScale };
		inline constexpr ScalarField kFuzzScalars[]{ ScalarField::kColor, ScalarField::kWeight };
		inline constexpr ScalarField kGlintScalars[]{ ScalarField::kScreenSpaceScale, ScalarField::kLogMicrofacetDensity, ScalarField::kMicrofacetRoughness, ScalarField::kDensityRandomization };
		inline constexpr ScalarField kCoatScalars[]{ ScalarField::kRoughness, ScalarField::kLevel };
		inline constexpr ScalarField kSubsurfaceScalars[]{ ScalarField::kColor, ScalarField::kThickness };

		// CS evaluates one of coat, subsurface and fuzz per material, and
		// glint excludes fuzz; every other pair coexists.
		inline constexpr Slot kFuzzExcludes[]{ Slot::kGlint, Slot::kCoat, Slot::kSubsurface };
		inline constexpr Slot kGlintExcludes[]{ Slot::kFuzz };
		inline constexpr Slot kCoatExcludes[]{ Slot::kFuzz, Slot::kSubsurface };
		inline constexpr Slot kSubsurfaceExcludes[]{ Slot::kFuzz, Slot::kCoat };
	}

	// Channels and notes as CS reads each map (BSLightingShaderMaterialPBR.h).
	inline constexpr SlotRow kSlots[]{
		{ Slot::kDiffuse, "diffuse", SlotColumns::kRgba, "r, g, b: albedo; a: on a shell, per-texel visibility (with the shell's alpha blend and alpha test)", {}, false, {}, MaterialMap::kDiffuse },
		{ Slot::kEmissive, "emissive", SlotColumns::kRgb, "r, g, b: emitted colour, scaled by strength; no alpha", SlotColumns::kEmissiveScalars, true, {}, MaterialMap::kNone },
		{ Slot::kRmaos, "rmaos", SlotColumns::kRgba, "r: roughness; g: metallic; b: ambient occlusion; a: reflectance (f0)", {}, false, {}, MaterialMap::kRmaos },
		{ Slot::kNormal, "normal", SlotColumns::kRgb, "r, g, b: tangent-space normal; the normal blend reorients rather than replaces; no alpha", {}, false, {}, MaterialMap::kNormal },
		{ Slot::kHeight, "height", SlotColumns::kRedOnly, "r only: height, offset by (r - 0.5) * scale; green, blue and alpha are never read", SlotColumns::kHeightScalars, true, {}, MaterialMap::kDisplacement },
		{ Slot::kFuzz, "fuzz", SlotColumns::kRgba, "r, g, b: fuzz colour; a: fuzz weight (the scalars set the base, the map modulates)", SlotColumns::kFuzzScalars, true, SlotColumns::kFuzzExcludes, MaterialMap::kNone },
		{ Slot::kGlint, "glint", SlotColumns::kNoMap, "no texture: glint is its four scalars alone; channels do not apply", SlotColumns::kGlintScalars, false, SlotColumns::kGlintExcludes, MaterialMap::kNone },
		{ Slot::kCoat, "coat", SlotColumns::kRgba, "r, g, b: coat colour; a: coat strength; shares one map with subsurface, so a material takes one of the two", SlotColumns::kCoatScalars, true, SlotColumns::kCoatExcludes, MaterialMap::kNone },
		{ Slot::kSubsurface, "subsurface", SlotColumns::kRgba, "r, g, b: subsurface colour; a: thickness; shares one map with coat, so a material takes one of the two", SlotColumns::kSubsurfaceScalars, true, SlotColumns::kSubsurfaceExcludes, MaterialMap::kNone },
	};
	static_assert(std::size(kSlots) == kSlotCount);
	inline constexpr Named<Blend> kBlends[]{ { Blend::kReplace, "replace" }, { Blend::kMultiply, "multiply" }, { Blend::kAdd, "add" }, { Blend::kSubtract, "subtract" }, { Blend::kScreen, "screen" }, { Blend::kLerp, "lerp" }, { Blend::kNormal, "normal" } };

	inline constexpr Named<ShellMaterial> kShellMaterials[]{ { ShellMaterial::kPbrCopy, "pbrCopy" }, { ShellMaterial::kVanilla, "vanilla" } };
	inline constexpr Named<ShellBlend> kShellBlends[]{ { ShellBlend::kAdditive, "additive" }, { ShellBlend::kAlpha, "alpha" } };
}
