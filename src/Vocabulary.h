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
	inline constexpr Named<Slot> kSlots[]{ { Slot::kDiffuse, "diffuse" }, { Slot::kEmissive, "emissive" }, { Slot::kRmaos, "rmaos" }, { Slot::kNormal, "normal" }, { Slot::kHeight, "height" }, { Slot::kFuzz, "fuzz" }, { Slot::kGlint, "glint" }, { Slot::kCoat, "coat" }, { Slot::kSubsurface, "subsurface" } };
	static_assert(std::size(kSlots) == kSlotCount);
	inline constexpr Named<ScalarField> kScalarFields[]{ { ScalarField::kStrength, "strength" }, { ScalarField::kScale, "scale" }, { ScalarField::kColor, "color" }, { ScalarField::kWeight, "weight" }, { ScalarField::kScreenSpaceScale, "screenSpaceScale" }, { ScalarField::kLogMicrofacetDensity, "logMicrofacetDensity" }, { ScalarField::kMicrofacetRoughness, "microfacetRoughness" }, { ScalarField::kDensityRandomization, "densityRandomization" }, { ScalarField::kRoughness, "roughness" }, { ScalarField::kLevel, "level" }, { ScalarField::kThickness, "thickness" } };
	static_assert(std::size(kScalarFields) == kScalarFieldCount);
	inline constexpr Named<Blend> kBlends[]{ { Blend::kReplace, "replace" }, { Blend::kMultiply, "multiply" }, { Blend::kAdd, "add" }, { Blend::kSubtract, "subtract" }, { Blend::kScreen, "screen" }, { Blend::kLerp, "lerp" }, { Blend::kNormal, "normal" } };

	inline constexpr Named<ShellMaterial> kShellMaterials[]{ { ShellMaterial::kPbrCopy, "pbrCopy" }, { ShellMaterial::kVanilla, "vanilla" } };
	inline constexpr Named<ShellBlend> kShellBlends[]{ { ShellBlend::kAdditive, "additive" }, { ShellBlend::kAlpha, "alpha" } };
}
