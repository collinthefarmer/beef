#include "render/Binding.h"

namespace BetterEnchantmentEffects {
namespace {
RE::NiPointer<RE::NiSourceTexture> *
TextureFieldOf(PBRMaterialLayout &a_material, Slot a_slot) {
  switch (a_slot) {
  case Slot::kDiffuse:
    return &a_material.diffuseTexture;
  case Slot::kNormal:
    return &a_material.normalTexture;
  case Slot::kEmissive:
    return &a_material.emissiveTexture;
  case Slot::kRmaos:
    return &a_material.rmaosTexture;
  case Slot::kHeight:
    return &a_material.displacementTexture;
  case Slot::kFuzz:
    return &a_material.featuresTexture1;
  case Slot::kCoat:
  case Slot::kSubsurface:
    return &a_material.featuresTexture0;
  default:
    return nullptr;
  }
}

std::string TextureName(const RE::NiSourceTexture *a_texture) {
  if (!a_texture) {
    return "(none)";
  }
  return a_texture->name.c_str() ? a_texture->name.c_str() : "(unnamed)";
}

bool FuzzPossible(const PBRMaterialLayout &a_material) {
  return (a_material.pbrFlags & (kPbrTwoLayer | kPbrHairMarschner)) == 0;
}

RE::NiColor ToNi(const Vec3 &a_v) { return RE::NiColor{a_v.x, a_v.y, a_v.z}; }
}

SlotWriter::SlotWriter(PBRMaterialLayout *a_material,
                       RE::BSLightingShaderProperty *a_property)
    : material_(a_material), property_(a_property) {}

std::string SlotWriter::Problem(Slot a_slot) const {
  if (!material_ || !property_) {
    return "no material";
  }
  if (!TextureFieldOf(*material_, a_slot) && a_slot != Slot::kGlint) {
    return std::format("slot '{}' has no place on a PBR material",
                       SlotName(a_slot));
  }
  if (a_slot == Slot::kEmissive && !property_->emissiveColor) {
    return "the property has no emissive colour storage";
  }
  const bool hair = (material_->pbrFlags & kPbrHairMarschner) != 0;
  if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint ||
       a_slot == Slot::kCoat || a_slot == Slot::kSubsurface) &&
      hair) {
    return "the material has a hair model, which CS evaluates instead";
  }
  if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint) &&
      !FuzzPossible(*material_) && !coat_) {
    return "the material has a coat model, which CS evaluates instead of fuzz "
           "and glint";
  }
  if (a_slot == Slot::kFuzz && glint_) {
    return "glint was written on this material; fuzz and glint exclude each "
           "other";
  }
  if (a_slot == Slot::kGlint && fuzz_) {
    return "fuzz was written on this material; fuzz and glint exclude each "
           "other";
  }
  if (a_slot == Slot::kCoat &&
      (subsurface_ || (material_->pbrFlags & kPbrSubsurface))) {
    return "the material carries subsurface; coat and subsurface share one map";
  }
  if (a_slot == Slot::kSubsurface &&
      (coat_ || (material_->pbrFlags & kPbrTwoLayer))) {
    return "the material carries a coat; coat and subsurface share one map";
  }
  return {};
}

void SlotWriter::WriteTexture(Slot a_slot, RE::NiSourceTexture *a_texture) {
  RE::NiPointer<RE::NiSourceTexture> *field =
      material_ ? TextureFieldOf(*material_, a_slot) : nullptr;
  if (!field) {
    return;
  }
  std::optional<SavedTexture> &saved =
      textures_[static_cast<std::size_t>(a_slot)];
  if (!saved) {
    saved = SavedTexture{*field, field->get()};
  }
  if (a_slot == Slot::kFuzz) {
    EnableFuzz();
    SetFeature(kPbrFuzz, a_texture != nullptr);
  } else if (a_slot == Slot::kCoat) {
    EnableCoat();
    SetFeature(kPbrTwoLayer | kPbrColoredCoat, a_texture != nullptr);
  } else if (a_slot == Slot::kSubsurface) {
    EnableSubsurface();
    SetFeature(kPbrSubsurface, a_texture != nullptr);
  }
  RE::NiSourceTexture *target = a_texture ? a_texture : saved->original.get();
  if (field->get() != target) {
    *field = RE::NiPointer<RE::NiSourceTexture>{target};
  }
  saved->written = target;
}

void SlotWriter::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  if (!property_ || !property_->emissiveColor) {
    return;
  }
  if (!emissive_) {
    emissive_ =
        SavedEmissive{*property_->emissiveColor, property_->emissiveMult,
                      property_->flags.any(
                          RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit)};
    property_->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
  }
  *property_->emissiveColor = ToNi(a_color);
  property_->emissiveMult = a_multiplier;
}

void SlotWriter::SetFeature(std::uint32_t a_bits, bool a_on) {
  if (!material_) {
    return;
  }
  if (!flags_) {
    flags_ = material_->pbrFlags;
  }
  if (a_on) {
    material_->pbrFlags |= a_bits;
  } else {
    material_->pbrFlags = (material_->pbrFlags & ~a_bits) | (*flags_ & a_bits);
  }
}

void SlotWriter::EnableFuzz() {
  if (!material_ || fuzz_ || !FuzzPossible(*material_)) {
    return;
  }
  fuzz_ = SavedFuzz{material_->fuzzColor, material_->fuzzWeight};
  SetFeature(kPbrFuzz, true);
}

void SlotWriter::WriteFuzz(const Vec3 &a_color, float a_weight) {
  EnableFuzz();
  if (!fuzz_) {
    return;
  }
  material_->fuzzColor = ToNi(a_color);
  material_->fuzzWeight = std::clamp(a_weight, 0.0f, 1.0f);
}

void SlotWriter::WriteGlint(float a_screenSpaceScale,
                            float a_logMicrofacetDensity,
                            float a_microfacetRoughness,
                            float a_densityRandomization, bool a_enabled) {
  if (!material_) {
    return;
  }
  if (!glint_) {
    glint_ = SavedGlint{material_->glintParameters};
  }
  GlintParameters &g = material_->glintParameters;
  g.enabled = a_enabled;
  g.screenSpaceScale = a_screenSpaceScale;
  g.logMicrofacetDensity = a_logMicrofacetDensity;
  g.microfacetRoughness = a_microfacetRoughness;
  g.densityRandomization = a_densityRandomization;
}

void SlotWriter::EnableCoat() {
  if (!material_ || coat_) {
    return;
  }
  coat_ = SavedCoat{material_->coatRoughness, material_->coatSpecularLevel};
  SetFeature(kPbrTwoLayer | kPbrColoredCoat, true);
}

void SlotWriter::WriteCoat(float a_roughness, float a_level) {
  EnableCoat();
  if (!coat_) {
    return;
  }
  material_->coatRoughness = std::clamp(a_roughness, 0.0f, 1.0f);
  material_->coatSpecularLevel = std::clamp(a_level, 0.0f, 1.0f);
}

void SlotWriter::EnableSubsurface() {
  if (!material_ || subsurface_) {
    return;
  }
  subsurface_ = SavedSubsurface{material_->specularColor,
                                material_->subSurfaceLightRolloff};
  SetFeature(kPbrSubsurface, true);
}

void SlotWriter::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  EnableSubsurface();
  if (!subsurface_) {
    return;
  }
  material_->specularColor = ToNi(a_color);
  material_->subSurfaceLightRolloff = std::clamp(a_thickness, 0.0f, 1.0f);
}

void SlotWriter::WriteHeightScale(float a_scale) {
  if (!material_) {
    return;
  }
  if (!heightScale_) {
    heightScale_ = material_->rimLightPower;
  }
  material_->rimLightPower = a_scale;
}

bool SlotWriter::StillOwned() const noexcept {
  if (!material_ || !property_ || property_->material != material_) {
    return false;
  }
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    const std::optional<SavedTexture> &saved = textures_[i];
    if (!saved) {
      continue;
    }
    const RE::NiPointer<RE::NiSourceTexture> *field =
        TextureFieldOf(*material_, static_cast<Slot>(i));
    if (field && field->get() != saved->written) {
      return false;
    }
  }
  return true;
}

void SlotWriter::Restore() {
  if (!StillOwned()) {
    return;
  }
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    if (const std::optional<SavedTexture> &saved = textures_[i]) {
      *TextureFieldOf(*material_, static_cast<Slot>(i)) = saved->original;
    }
  }
  if (emissive_ && property_->emissiveColor) {
    *property_->emissiveColor = emissive_->color;
    property_->emissiveMult = emissive_->multiplier;
    if (!emissive_->ownEmit) {
      property_->flags.reset(
          RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
    }
  }
  if (fuzz_) {
    material_->fuzzColor = fuzz_->color;
    material_->fuzzWeight = fuzz_->weight;
  }
  if (heightScale_) {
    material_->rimLightPower = *heightScale_;
  }
  if (glint_) {
    material_->glintParameters = glint_->parameters;
  }
  if (coat_) {
    material_->coatRoughness = coat_->roughness;
    material_->coatSpecularLevel = coat_->level;
  }
  if (subsurface_) {
    material_->specularColor = subsurface_->color;
    material_->subSurfaceLightRolloff = subsurface_->rolloff;
  }
  if (flags_) {
    material_->pbrFlags = *flags_;
  }
  flags_.reset();
  textures_ = {};
  emissive_.reset();
  fuzz_.reset();
  glint_.reset();
  coat_.reset();
  subsurface_.reset();
  heightScale_.reset();
}

std::vector<SlotState> SlotWriter::Slots() const {
  std::vector<SlotState> out;
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    if (const std::optional<SavedTexture> &saved = textures_[i]) {
      out.push_back({static_cast<Slot>(i), TextureName(saved->original.get()),
                     TextureName(saved->written)});
    }
  }
  return out;
}

std::unique_ptr<MaterialBinding>
MaterialBinding::Install(RE::BSGeometry *a_geometry,
                         RE::BSLightingShaderProperty *a_property,
                         bool a_uniqueCopy) {
  if (!a_geometry || !a_property || !a_property->material ||
      !IsPBRProperty(a_property)) {
    return nullptr;
  }
  std::unique_ptr<MaterialBinding> binding{new MaterialBinding{}};
  binding->geometry_ = RE::NiPointer{a_geometry};
  binding->property_ = RE::NiPointer{a_property};
  if (a_uniqueCopy) {
    RE::BSShaderMaterial *original = a_property->material;
    binding->original_ = RE::BSTSmartPointer<RE::BSShaderMaterial>{original};
    a_property->SetMaterial(original, true);
    if (a_property->material == original) {
      RE::BSShaderMaterial *copy = original->Create();
      if (copy) {
        copy->CopyMembers(original);
        a_property->SetMaterial(copy, true);
      }
    }
    if (a_property->material == original) {
      logger::warn("could not give '{}' a private material; leaving it shared",
                   a_geometry->name.c_str());
      binding->original_.reset();
    }
  }
  binding->material_ = static_cast<PBRMaterialLayout *>(a_property->material);
  binding->slots_ = SlotWriter{binding->material_, a_property};
  return binding;
}

MaterialBinding::~MaterialBinding() {
  RE::BSLightingShaderProperty *property = property_.get();
  if (!property) {
    return;
  }
  if (!StillOwned()) {
    logger::info("restore skipped: '{}' changed hands",
                 geometry_ ? geometry_->name.c_str() : "?");
    return;
  }
  slots_.Restore();
  if (original_) {
    property->SetMaterial(original_.get(), true);
  }
}

PBRMaterialLayout *MaterialBinding::Material() const noexcept {
  return material_;
}

RE::BSLightingShaderProperty *MaterialBinding::Property() const noexcept {
  return property_.get();
}

bool MaterialBinding::Private() const noexcept {
  return static_cast<bool>(original_);
}

std::string MaterialBinding::Problem(Slot a_slot) const {
  return slots_.Problem(a_slot);
}

void MaterialBinding::WriteTexture(Slot a_slot,
                                   RE::NiSourceTexture *a_texture) {
  slots_.WriteTexture(a_slot, a_texture);
}

void MaterialBinding::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  slots_.WriteEmissive(a_color, a_multiplier);
}

void MaterialBinding::WriteFuzz(const Vec3 &a_color, float a_weight) {
  slots_.WriteFuzz(a_color, a_weight);
}

void MaterialBinding::WriteHeightScale(float a_scale) {
  slots_.WriteHeightScale(a_scale);
}

void MaterialBinding::WriteGlint(float a_screenSpaceScale,
                                 float a_logMicrofacetDensity,
                                 float a_microfacetRoughness,
                                 float a_densityRandomization, bool a_enabled) {
  slots_.WriteGlint(a_screenSpaceScale, a_logMicrofacetDensity,
                    a_microfacetRoughness, a_densityRandomization, a_enabled);
}

void MaterialBinding::WriteCoat(float a_roughness, float a_level) {
  slots_.WriteCoat(a_roughness, a_level);
}

void MaterialBinding::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  slots_.WriteSubsurface(a_color, a_thickness);
}

std::vector<SlotState> MaterialBinding::Slots() const { return slots_.Slots(); }

bool MaterialBinding::StillOwned() const noexcept {
  return slots_.StillOwned();
}
}
