#include "render/Binding.h"

#include <type_traits>
#include <utility>

namespace BetterEnchantmentEffects {
static_assert(!std::is_default_constructible_v<PbrMaterial>);
static_assert(
    !std::is_constructible_v<PbrMaterial, RE::BSLightingShaderProperty *>);
static_assert(!std::is_default_constructible_v<SlotWriter>);
static_assert(!std::is_copy_constructible_v<SlotWriter>);
static_assert(!std::is_constructible_v<SlotWriter, PBRMaterialLayout *,
                                       RE::BSLightingShaderProperty *>);

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

SlotWriter::SlotWriter(PbrMaterial a_material)
    : binding_(std::move(a_material)) {}

bool SlotWriter::MaterialAttached() const noexcept {
  return binding_.Attached();
}

std::string SlotWriter::Problem(Slot a_slot) const {
  if (!MaterialAttached()) {
    return "material unavailable or replaced";
  }
  if (!TextureFieldOf(*binding_.material_, a_slot) && a_slot != Slot::kGlint) {
    return std::format("slot '{}' has no place on a PBR material",
                       SlotName(a_slot));
  }
  if (a_slot == Slot::kEmissive && !binding_.property_->emissiveColor) {
    return "the property has no emissive colour storage";
  }
  const bool hair = (binding_.material_->pbrFlags & kPbrHairMarschner) != 0;
  if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint ||
       a_slot == Slot::kCoat || a_slot == Slot::kSubsurface) &&
      hair) {
    return "the material has a hair model, which CS evaluates instead";
  }
  if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint) &&
      !FuzzPossible(*binding_.material_) && !coat_) {
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
      (subsurface_ || (binding_.material_->pbrFlags & kPbrSubsurface))) {
    return "the material carries subsurface; coat and subsurface share one map";
  }
  if (a_slot == Slot::kSubsurface &&
      (coat_ || (binding_.material_->pbrFlags & kPbrTwoLayer))) {
    return "the material carries a coat; coat and subsurface share one map";
  }
  return {};
}

void SlotWriter::WriteTexture(Slot a_slot, RE::NiSourceTexture *a_texture) {
  if (!MaterialAttached()) {
    return;
  }
  RE::NiPointer<RE::NiSourceTexture> *field =
      binding_.material_ ? TextureFieldOf(*binding_.material_, a_slot)
                         : nullptr;
  if (!field) {
    return;
  }
  std::optional<SavedTexture> &saved =
      textures_[static_cast<std::size_t>(a_slot)];
  if (!saved) {
    saved = SavedTexture{*field, *field};
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
  saved->written = *field;
}

void SlotWriter::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  if (!MaterialAttached() || !binding_.property_->emissiveColor) {
    return;
  }
  if (!emissive_) {
    emissive_ = SavedEmissive{
        *binding_.property_->emissiveColor, binding_.property_->emissiveMult,
        binding_.property_->flags.any(
            RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit)};
    binding_.property_->flags.set(
        RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
  }
  *binding_.property_->emissiveColor = ToNi(a_color);
  binding_.property_->emissiveMult = a_multiplier;
}

void SlotWriter::SetFeature(std::uint32_t a_bits, bool a_on) {
  if (!binding_.material_) {
    return;
  }
  if (!flags_) {
    flags_ = binding_.material_->pbrFlags;
  }
  if (a_on) {
    binding_.material_->pbrFlags |= a_bits;
  } else {
    binding_.material_->pbrFlags =
        (binding_.material_->pbrFlags & ~a_bits) | (*flags_ & a_bits);
  }
}

void SlotWriter::EnableFuzz() {
  if (!binding_.material_ || fuzz_ || !FuzzPossible(*binding_.material_)) {
    return;
  }
  fuzz_ =
      SavedFuzz{binding_.material_->fuzzColor, binding_.material_->fuzzWeight};
  SetFeature(kPbrFuzz, true);
}

void SlotWriter::WriteFuzz(const Vec3 &a_color, float a_weight) {
  if (!MaterialAttached()) {
    return;
  }
  EnableFuzz();
  if (!fuzz_) {
    return;
  }
  binding_.material_->fuzzColor = ToNi(a_color);
  binding_.material_->fuzzWeight = std::clamp(a_weight, 0.0f, 1.0f);
}

void SlotWriter::WriteGlint(float a_screenSpaceScale,
                            float a_logMicrofacetDensity,
                            float a_microfacetRoughness,
                            float a_densityRandomization, bool a_enabled) {
  if (!MaterialAttached()) {
    return;
  }
  if (!glint_) {
    glint_ = SavedGlint{binding_.material_->glintParameters};
  }
  GlintParameters &g = binding_.material_->glintParameters;
  g.enabled = a_enabled;
  g.screenSpaceScale = a_screenSpaceScale;
  g.logMicrofacetDensity = a_logMicrofacetDensity;
  g.microfacetRoughness = a_microfacetRoughness;
  g.densityRandomization = a_densityRandomization;
}

void SlotWriter::EnableCoat() {
  if (!binding_.material_ || coat_) {
    return;
  }
  coat_ = SavedCoat{binding_.material_->coatRoughness,
                    binding_.material_->coatSpecularLevel};
  SetFeature(kPbrTwoLayer | kPbrColoredCoat, true);
}

void SlotWriter::WriteCoat(float a_roughness, float a_level) {
  if (!MaterialAttached()) {
    return;
  }
  EnableCoat();
  if (!coat_) {
    return;
  }
  binding_.material_->coatRoughness = std::clamp(a_roughness, 0.0f, 1.0f);
  binding_.material_->coatSpecularLevel = std::clamp(a_level, 0.0f, 1.0f);
}

void SlotWriter::EnableSubsurface() {
  if (!binding_.material_ || subsurface_) {
    return;
  }
  subsurface_ = SavedSubsurface{binding_.material_->specularColor,
                                binding_.material_->subSurfaceLightRolloff};
  SetFeature(kPbrSubsurface, true);
}

void SlotWriter::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  if (!MaterialAttached()) {
    return;
  }
  EnableSubsurface();
  if (!subsurface_) {
    return;
  }
  binding_.material_->specularColor = ToNi(a_color);
  binding_.material_->subSurfaceLightRolloff =
      std::clamp(a_thickness, 0.0f, 1.0f);
}

void SlotWriter::WriteHeightScale(float a_scale) {
  if (!MaterialAttached()) {
    return;
  }
  if (!heightScale_) {
    heightScale_ = binding_.material_->rimLightPower;
  }
  binding_.material_->rimLightPower = a_scale;
}

bool SlotWriter::StillOwned() const noexcept {
  if (!MaterialAttached()) {
    return false;
  }
  for (std::size_t i = 0; i < kSlotCount; ++i) {
    const std::optional<SavedTexture> &saved = textures_[i];
    if (!saved) {
      continue;
    }
    const RE::NiPointer<RE::NiSourceTexture> *field =
        TextureFieldOf(*binding_.material_, static_cast<Slot>(i));
    if (field && field->get() != saved->written.get()) {
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
      *TextureFieldOf(*binding_.material_, static_cast<Slot>(i)) =
          saved->original;
    }
  }
  if (emissive_ && binding_.property_->emissiveColor) {
    *binding_.property_->emissiveColor = emissive_->color;
    binding_.property_->emissiveMult = emissive_->multiplier;
    if (!emissive_->ownEmit) {
      binding_.property_->flags.reset(
          RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
    }
  }
  if (fuzz_) {
    binding_.material_->fuzzColor = fuzz_->color;
    binding_.material_->fuzzWeight = fuzz_->weight;
  }
  if (heightScale_) {
    binding_.material_->rimLightPower = *heightScale_;
  }
  if (glint_) {
    binding_.material_->glintParameters = glint_->parameters;
  }
  if (coat_) {
    binding_.material_->coatRoughness = coat_->roughness;
    binding_.material_->coatSpecularLevel = coat_->level;
  }
  if (subsurface_) {
    binding_.material_->specularColor = subsurface_->color;
    binding_.material_->subSurfaceLightRolloff = subsurface_->rolloff;
  }
  if (flags_) {
    binding_.material_->pbrFlags = *flags_;
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
                     TextureName(saved->written.get())});
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
      const RE::BSTSmartPointer<RE::BSShaderMaterial> copy{original->Create()};
      if (copy) {
        copy->CopyMembers(original);
        a_property->SetMaterial(copy.get(), true);
      }
    }
    if (a_property->material == original) {
      logger::warn("could not give '{}' a private material; binding dropped",
                   a_geometry->name.c_str());
      return nullptr;
    }
  }
  const std::optional<PbrMaterial> material = PbrMaterial::Bind(a_property);
  if (!material) {
    if (binding->original_) {
      a_property->SetMaterial(binding->original_.get(), true);
    }
    return nullptr;
  }
  binding->slots_.emplace(*material);
  return binding;
}

MaterialBinding::~MaterialBinding() {
  RE::BSLightingShaderProperty *property = property_.get();
  if (!property) {
    return;
  }
  if (!slots_ || !StillOwned()) {
    logger::info("restore skipped: '{}' changed hands",
                 geometry_ ? geometry_->name.c_str() : "?");
    return;
  }
  slots_->Restore();
  if (original_) {
    property->SetMaterial(original_.get(), true);
  }
}

RE::BSLightingShaderProperty *MaterialBinding::Property() const noexcept {
  return property_.get();
}

bool MaterialBinding::Private() const noexcept {
  return static_cast<bool>(original_);
}

std::string MaterialBinding::Problem(Slot a_slot) const {
  return slots_ ? slots_->Problem(a_slot) : "material unavailable";
}

void MaterialBinding::WriteTexture(Slot a_slot,
                                   RE::NiSourceTexture *a_texture) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteTexture(a_slot, a_texture);
}

void MaterialBinding::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteEmissive(a_color, a_multiplier);
}

void MaterialBinding::WriteFuzz(const Vec3 &a_color, float a_weight) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteFuzz(a_color, a_weight);
}

void MaterialBinding::WriteHeightScale(float a_scale) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteHeightScale(a_scale);
}

void MaterialBinding::WriteGlint(float a_screenSpaceScale,
                                 float a_logMicrofacetDensity,
                                 float a_microfacetRoughness,
                                 float a_densityRandomization, bool a_enabled) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteGlint(a_screenSpaceScale, a_logMicrofacetDensity,
                     a_microfacetRoughness, a_densityRandomization, a_enabled);
}

void MaterialBinding::WriteCoat(float a_roughness, float a_level) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteCoat(a_roughness, a_level);
}

void MaterialBinding::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteSubsurface(a_color, a_thickness);
}

std::vector<SlotState> MaterialBinding::Slots() const {
  return slots_ ? slots_->Slots() : std::vector<SlotState>{};
}

bool MaterialBinding::StillOwned() const noexcept {
  return geometry_ &&
         geometry_->GetGeometryRuntimeData()
                 .properties[RE::BSGeometry::States::kEffect]
                 .get() == property_.get() &&
         slots_ && slots_->StillOwned();
}
}
