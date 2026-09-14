#pragma once

#include "Core.h"
#include "recipe/Recipe.h"

#include <iterator>

namespace BetterEnchantmentEffects {
inline constexpr KeyKindSpec kKeyKinds[]{
    {KeyKind::kDefault, "default", 0, KeyOperand::kNone, false, nullptr,
     nullptr},
    {KeyKind::kMaterial, "material", 10, KeyOperand::kGlob, false, nullptr,
     nullptr},
    {KeyKind::kKeyword, "keyword", 20, KeyOperand::kForm, false, nullptr,
     &WornPiece::keywords},
    {KeyKind::kArmor, "armor", 30, KeyOperand::kForm, false, &WornPiece::armor,
     nullptr},
    {KeyKind::kEffectShader, "effectShader", 40, KeyOperand::kForm, true,
     &WornPiece::effectShader, nullptr},
    {KeyKind::kEnchantment, "enchantment", 50, KeyOperand::kForm, true,
     &WornPiece::enchantment, nullptr},
    {KeyKind::kMagicEffect, "magicEffect", 60, KeyOperand::kForm, true,
     &WornPiece::magicEffect, nullptr},
};
static_assert(Complete(kKeyKinds, kKeyKindCount));
inline constexpr Named<SelectorKind> kSelectorKinds[]{
    {SelectorKind::kAddon, "addon"},
    {SelectorKind::kGeometry, "geometry"},
    {SelectorKind::kTexture, "texture"}};
static_assert(Complete(kSelectorKinds, kSelectorKindCount));

inline constexpr Named<Waveform> kWaveforms[]{{Waveform::kSine, "sine"},
                                              {Waveform::kTriangle, "triangle"},
                                              {Waveform::kSquare, "square"},
                                              {Waveform::kSaw, "saw"}};
static_assert(Complete(kWaveforms, kWaveformCount));
inline constexpr Named<EfshField> kEfshFields[]{
    {EfshField::kFillAlpha, "fillAlpha"},
    {EfshField::kFillColor, "fillColor"},
    {EfshField::kEdgeAlpha, "edgeAlpha"},
    {EfshField::kEdgeColor, "edgeColor"},
    {EfshField::kScroll, "scroll"}};
static_assert(Complete(kEfshFields, kEfshFieldCount));
inline constexpr Named<Measure> kMeasures[]{
    {Measure::kCurrent, "current"},
    {Measure::kBase, "base"},
    {Measure::kPermanent, "permanent"},
    {Measure::kTemporaryModifier, "temporaryModifier"},
    {Measure::kDamage, "damage"},
    {Measure::kMax, "max"}};
static_assert(Complete(kMeasures, kMeasureCount));
inline constexpr Named<ActorStateKind> kActorStates[]{
    {ActorStateKind::kInCombat, "inCombat"},
    {ActorStateKind::kSneaking, "sneaking"},
    {ActorStateKind::kWeaponDrawn, "weaponDrawn"},
    {ActorStateKind::kHostileDistance, "hostileDistance"}};
static_assert(Complete(kActorStates, kActorStateCount));
inline constexpr Named<EnchantmentField> kEnchantmentFields[]{
    {EnchantmentField::kMagnitude, "magnitude"},
    {EnchantmentField::kCost, "cost"}};
static_assert(Complete(kEnchantmentFields, kEnchantmentFieldCount));
inline constexpr Named<PayloadField> kPayloadFields[]{
    {PayloadField::kValue, "value"},
    {PayloadField::kPosition, "position"},
    {PayloadField::kNormal, "normal"}};
static_assert(Complete(kPayloadFields, kPayloadFieldCount));

inline constexpr ImageChannelSpec kImageChannels[]{
    {ImageChannel::kRgb, "rgb", ShaderChannel::kRgb},
    {ImageChannel::kR, "r", ShaderChannel::kR},
    {ImageChannel::kG, "g", ShaderChannel::kG},
    {ImageChannel::kB, "b", ShaderChannel::kB},
    {ImageChannel::kA, "a", ShaderChannel::kA},
    {ImageChannel::kLuma, "luma", ShaderChannel::kLuma},
};
static_assert(Complete(kImageChannels, kImageChannelCount));
inline constexpr Named<ImageSpace> kImageSpaces[]{{ImageSpace::kTiled, "tiled"},
                                                  {ImageSpace::kMesh, "mesh"}};
static_assert(Complete(kImageSpaces, kImageSpaceCount));
inline constexpr MaterialChannelSpec kMaterialChannels[]{
    {MaterialChannel::kDiffuseRgb, "diffuseRgb", MaterialMap::kDiffuse,
     ShaderChannel::kRgb, ValueType::kVec3},
    {MaterialChannel::kDiffuseLuma, "diffuseLuma", MaterialMap::kDiffuse,
     ShaderChannel::kLuma, ValueType::kScalar},
    {MaterialChannel::kNormalSlope, "normalSlope", MaterialMap::kNone,
     ShaderChannel::kR, ValueType::kScalar},
    {MaterialChannel::kRoughness, "roughness", MaterialMap::kRmaos,
     ShaderChannel::kR, ValueType::kScalar},
    {MaterialChannel::kMetallic, "metallic", MaterialMap::kRmaos,
     ShaderChannel::kG, ValueType::kScalar},
    {MaterialChannel::kOcclusion, "occlusion", MaterialMap::kRmaos,
     ShaderChannel::kB, ValueType::kScalar},
    {MaterialChannel::kReflectance, "reflectance", MaterialMap::kRmaos,
     ShaderChannel::kA, ValueType::kScalar},
    {MaterialChannel::kDisplacement, "displacement", MaterialMap::kDisplacement,
     ShaderChannel::kR, ValueType::kScalar},
    {MaterialChannel::kRelief, "relief", MaterialMap::kNone, ShaderChannel::kR,
     ValueType::kScalar},
};
static_assert(Complete(kMaterialChannels, kMaterialChannelCount));
inline constexpr Named<UvAxis> kUvAxes[]{{UvAxis::kU, "u"}, {UvAxis::kV, "v"}};
static_assert(Complete(kUvAxes, kUvAxisCount));
inline constexpr Named<RippleShape> kRippleShapes[]{
    {RippleShape::kRing, "ring"}, {RippleShape::kDisc, "disc"}};
static_assert(Complete(kRippleShapes, kRippleShapeCount));
inline constexpr Named<SourceKindId> kSourceKindWords[]{
    {SourceKindId::kImage, "image"},
    {SourceKindId::kMaterial, "material"},
    {SourceKindId::kBake, "bake"},
    {SourceKindId::kUv, "uv"},
    {SourceKindId::kDistance, "distance"},
    {SourceKindId::kRipple, "ripple"},
    {SourceKindId::kMaterialClusters, "materialClusters"}};
static_assert(Complete(kSourceKindWords, kSourceKindCount));
static_assert(
    std::is_same_v<std::variant_alternative_t<0, SourceKind>, ImageSource> &&
    std::is_same_v<std::variant_alternative_t<1, SourceKind>, MaterialSource> &&
    std::is_same_v<std::variant_alternative_t<2, SourceKind>, BakeSource> &&
    std::is_same_v<std::variant_alternative_t<3, SourceKind>, UvSource> &&
    std::is_same_v<std::variant_alternative_t<4, SourceKind>, DistanceSource> &&
    std::is_same_v<std::variant_alternative_t<5, SourceKind>, RippleSource> &&
    std::is_same_v<std::variant_alternative_t<6, SourceKind>,
                   MaterialClustersSource>);
inline constexpr std::string_view kTriggerOriginWords[]{"event", "plugin",
                                                        "when"};
static_assert(std::size(kTriggerOriginWords) ==
              std::variant_size_v<TriggerOrigin>);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, TriggerOrigin>, EventOrigin> &&
    std::is_same_v<std::variant_alternative_t<1, TriggerOrigin>,
                   PluginOrigin> &&
    std::is_same_v<std::variant_alternative_t<2, TriggerOrigin>, WhenOrigin>);
inline constexpr std::string_view kBakeKindWords[]{
    "position",   "localPosition", "worldUp", "partition",
    "boneWeight", "componentId",   "chartId"};
static_assert(std::size(kBakeKindWords) == std::variant_size_v<BakeKind>);
static_assert(
    std::is_same_v<std::variant_alternative_t<0, BakeKind>, PositionBake> &&
    std::is_same_v<std::variant_alternative_t<1, BakeKind>,
                   LocalPositionBake> &&
    std::is_same_v<std::variant_alternative_t<2, BakeKind>, WorldUpBake> &&
    std::is_same_v<std::variant_alternative_t<3, BakeKind>, PartitionBake> &&
    std::is_same_v<std::variant_alternative_t<4, BakeKind>, BoneWeightBake> &&
    std::is_same_v<std::variant_alternative_t<5, BakeKind>, ComponentIdBake> &&
    std::is_same_v<std::variant_alternative_t<6, BakeKind>, ChartIdBake>);

inline constexpr BipedSlotSpec kBipedSlots[]{
    {30, "head"},     {31, "hair"},   {32, "body"}, {33, "hands"},
    {34, "forearms"}, {35, "amulet"}, {36, "ring"}, {37, "feet"},
    {38, "calves"},   {39, "shield"}, {40, "tail"}};

inline constexpr SignalKindSpec kSignalKinds[]{
    {SignalKindId::kConstant, "constant", true},
    {SignalKindId::kPulse, "pulse", false},
    {SignalKindId::kRamp, "ramp", false},
    {SignalKindId::kEfsh, "efsh", false},
    {SignalKindId::kActorValue, "av", false},
    {SignalKindId::kActorState, "actorState", false},
    {SignalKindId::kEnchantment, "enchantment", false},
    {SignalKindId::kTrigger, "trigger", false},
    {SignalKindId::kPayload, "payload", false},
    {SignalKindId::kCounter, "counter", false},
    {SignalKindId::kAccumulate, "accumulate", false},
    {SignalKindId::kNoise, "noise", false},
    {SignalKindId::kGradient, "gradient", false},
    {SignalKindId::kDelta, "delta", false},
    {SignalKindId::kSmooth, "smooth", false},
    {SignalKindId::kExpr, "expr", true},
};
static_assert(Complete(kSignalKinds, kSignalKindCount));

inline constexpr Named<Surface> kSurfaces[]{{Surface::kMaterial, "material"},
                                            {Surface::kShell, "shell"}};
static_assert(Complete(kSurfaces, kSurfaceCount));
inline constexpr ScalarFieldSpec kScalarFields[]{
    {ScalarField::kStrength, "strength", &SlotScalars::strength, 1.0f},
    {ScalarField::kScale, "scale", &SlotScalars::scale, 1.0f},
    {ScalarField::kColor, "color", &SlotScalars::color, 1.0f},
    {ScalarField::kWeight, "weight", &SlotScalars::weight, 1.0f},
    {ScalarField::kScreenSpaceScale, "screenSpaceScale",
     &SlotScalars::screenSpaceScale, 1.5f},
    {ScalarField::kLogMicrofacetDensity, "logMicrofacetDensity",
     &SlotScalars::logMicrofacetDensity, 40.0f},
    {ScalarField::kMicrofacetRoughness, "microfacetRoughness",
     &SlotScalars::microfacetRoughness, 0.015f},
    {ScalarField::kDensityRandomization, "densityRandomization",
     &SlotScalars::densityRandomization, 2.0f},
    {ScalarField::kRoughness, "roughness", &SlotScalars::roughness, 0.15f},
    {ScalarField::kLevel, "level", &SlotScalars::level, 0.6f},
    {ScalarField::kThickness, "thickness", &SlotScalars::thickness, 1.0f},
};
static_assert(Complete(kScalarFields, kScalarFieldCount));

namespace SlotColumns {
inline constexpr ChannelSet kRgba{true, true, true, true};
inline constexpr ChannelSet kRgb{true, true, true, false};
inline constexpr ChannelSet kRedOnly{true, false, false, false};
inline constexpr ChannelSet kNoMap{false, false, false, false};

inline constexpr ScalarField kEmissiveScalars[]{ScalarField::kStrength};
inline constexpr ScalarField kHeightScalars[]{ScalarField::kScale};
inline constexpr ScalarField kFuzzScalars[]{ScalarField::kColor,
                                            ScalarField::kWeight};
inline constexpr ScalarField kGlintScalars[]{
    ScalarField::kScreenSpaceScale, ScalarField::kLogMicrofacetDensity,
    ScalarField::kMicrofacetRoughness, ScalarField::kDensityRandomization};
inline constexpr ScalarField kCoatScalars[]{ScalarField::kRoughness,
                                            ScalarField::kLevel};
inline constexpr ScalarField kSubsurfaceScalars[]{ScalarField::kColor,
                                                  ScalarField::kThickness};

inline constexpr Slot kFuzzExcludes[]{Slot::kGlint, Slot::kCoat,
                                      Slot::kSubsurface};
inline constexpr Slot kGlintExcludes[]{Slot::kFuzz};
inline constexpr Slot kCoatExcludes[]{Slot::kFuzz, Slot::kSubsurface};
inline constexpr Slot kSubsurfaceExcludes[]{Slot::kFuzz, Slot::kCoat};
}

inline constexpr SlotSpec kSlots[]{
    {Slot::kDiffuse,
     "diffuse",
     SlotColumns::kRgba,
     "r, g, b: albedo; a: on a shell, per-texel visibility (with the shell's "
     "alpha blend and alpha test)",
     {},
     false,
     {},
     MaterialMap::kDiffuse},
    {Slot::kEmissive,
     "emissive",
     SlotColumns::kRgb,
     "r, g, b: emitted colour, scaled by strength; no alpha",
     SlotColumns::kEmissiveScalars,
     true,
     {},
     MaterialMap::kNone},
    {Slot::kRmaos,
     "rmaos",
     SlotColumns::kRgba,
     "r: roughness; g: metallic; b: ambient occlusion; a: reflectance (f0)",
     {},
     false,
     {},
     MaterialMap::kRmaos},
    {Slot::kNormal,
     "normal",
     SlotColumns::kRgb,
     "r, g, b: tangent-space normal; the normal blend reorients rather than "
     "replaces; no alpha",
     {},
     false,
     {},
     MaterialMap::kNormal},
    {Slot::kHeight,
     "height",
     SlotColumns::kRedOnly,
     "r only: height, offset by (r - 0.5) * scale; green, blue and alpha are "
     "never read",
     SlotColumns::kHeightScalars,
     true,
     {},
     MaterialMap::kDisplacement},
    {Slot::kFuzz, "fuzz", SlotColumns::kRgba,
     "r, g, b: fuzz colour; a: fuzz weight (the scalars set the base, the map "
     "modulates)",
     SlotColumns::kFuzzScalars, true, SlotColumns::kFuzzExcludes,
     MaterialMap::kNone},
    {Slot::kGlint, "glint", SlotColumns::kNoMap,
     "no texture: glint is its four scalars alone; channels do not apply",
     SlotColumns::kGlintScalars, false, SlotColumns::kGlintExcludes,
     MaterialMap::kNone},
    {Slot::kCoat, "coat", SlotColumns::kRgba,
     "r, g, b: coat colour; a: coat strength; shares one map with subsurface, "
     "so a material takes one of the two",
     SlotColumns::kCoatScalars, true, SlotColumns::kCoatExcludes,
     MaterialMap::kNone},
    {Slot::kSubsurface, "subsurface", SlotColumns::kRgba,
     "r, g, b: subsurface colour; a: thickness; shares one map with coat, so a "
     "material takes one of the two",
     SlotColumns::kSubsurfaceScalars, true, SlotColumns::kSubsurfaceExcludes,
     MaterialMap::kNone},
};
static_assert(Complete(kSlots, kSlotCount));
inline constexpr BlendSpec kBlends[]{
    {Blend::kReplace, "replace", 0, false},
    {Blend::kMultiply, "multiply", 1, false},
    {Blend::kAdd, "add", 2, false},
    {Blend::kSubtract, "subtract", 3, false},
    {Blend::kScreen, "screen", 4, false},
    {Blend::kLerp, "lerp", 5, false},
    {Blend::kNormal, "normal", 6, true},
};
static_assert(Complete(kBlends, kBlendCount));

inline constexpr Named<ShellMaterial> kShellMaterials[]{
    {ShellMaterial::kPbrCopy, "pbrCopy"}, {ShellMaterial::kVanilla, "vanilla"}};
static_assert(Complete(kShellMaterials, kShellMaterialCount));
inline constexpr Named<ShellBlend> kShellBlends[]{
    {ShellBlend::kAdditive, "additive"}, {ShellBlend::kAlpha, "alpha"}};
static_assert(Complete(kShellBlends, kShellBlendCount));
}
