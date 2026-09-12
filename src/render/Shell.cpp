#include "render/Binding.h"

#include "Identity.h"

#include <algorithm>
#include <cstring>

namespace BetterEnchantmentEffects {
namespace {
RE::NiColor ToNi(const Vec3 &a_v) { return RE::NiColor{a_v.x, a_v.y, a_v.z}; }

RE::NiPointer<RE::NiAlphaProperty> CreateAlphaProperty(bool a_additive,
                                                       float a_alphaTest) {
  auto *property = RE::malloc<RE::NiAlphaProperty>();
  if (!property) {
    return nullptr;
  }
  std::memset(static_cast<void *>(property), 0, sizeof(RE::NiAlphaProperty));
  *reinterpret_cast<std::uintptr_t *>(property) =
      RE::VTABLE_NiAlphaProperty[0].address();
  property->SetAlphaBlending(true);
  property->SetSrcBlendMode(RE::NiAlphaProperty::AlphaFunction::kSrcAlpha);
  property->SetDestBlendMode(
      a_additive ? RE::NiAlphaProperty::AlphaFunction::kOne
                 : RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha);
  property->SetAlphaTesting(a_alphaTest > 0.0f);
  property->alphaThreshold =
      static_cast<std::uint8_t>(std::clamp(a_alphaTest, 0.0f, 1.0f) * 255.0f);
  return RE::NiPointer<RE::NiAlphaProperty>{property};
}

bool CopyBoneWeights(RE::NiSkinData::BoneData &a_copy,
                     const RE::NiSkinData::BoneData &a_source) {
  if (!a_source.boneVertData || a_source.verts == 0) {
    return true;
  }
  const std::size_t bytes =
      sizeof(RE::NiSkinData::BoneVertData) * a_source.verts;
  a_copy.boneVertData = RE::malloc<RE::NiSkinData::BoneVertData>(bytes);
  if (!a_copy.boneVertData) {
    return false;
  }
  std::memcpy(a_copy.boneVertData, a_source.boneVertData, bytes);
  return true;
}

RE::NiPointer<RE::NiSkinData> CopySkinData(const RE::NiSkinData &a_source) {
  if (a_source.bones && !a_source.boneData) {
    return nullptr;
  }
  auto *copy = RE::malloc<RE::NiSkinData>();
  if (!copy) {
    return nullptr;
  }
  std::memcpy(static_cast<void *>(copy), static_cast<const void *>(&a_source),
              sizeof(RE::NiSkinData));
  *reinterpret_cast<std::uintptr_t *>(copy) =
      RE::VTABLE_NiSkinData[0].address();
  reinterpret_cast<volatile std::uint32_t *>(copy)[2] = 0;
  std::memset(static_cast<void *>(&copy->skinPartition), 0,
              sizeof(copy->skinPartition));
  copy->skinPartition = a_source.skinPartition;
  copy->boneData = nullptr;
  copy->bones = 0;
  RE::NiPointer<RE::NiSkinData> owned{copy};
  if (a_source.bones) {
    auto *bones = RE::malloc<RE::NiSkinData::BoneData>(
        sizeof(RE::NiSkinData::BoneData) * a_source.bones);
    if (!bones) {
      return nullptr;
    }
    std::memcpy(static_cast<void *>(bones),
                static_cast<const void *>(a_source.boneData),
                sizeof(RE::NiSkinData::BoneData) * a_source.bones);
    for (std::uint32_t i = 0; i < a_source.bones; ++i) {
      bones[i].boneVertData = nullptr;
    }
    copy->boneData = bones;
    copy->bones = a_source.bones;
    for (std::uint32_t i = 0; i < copy->bones; ++i) {
      if (!CopyBoneWeights(bones[i], a_source.boneData[i])) {
        return nullptr;
      }
    }
  }
  return owned;
}
}

std::unique_ptr<ShellBinding>
ShellBinding::Create(RE::BSGeometry *a_original,
                     RE::BSLightingShaderProperty *a_property,
                     const ShellSettings &a_settings) {
  using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
  if (!a_original || !a_property || !a_property->material) {
    return nullptr;
  }
  auto *parent = a_original->parent;
  if (!parent) {
    return nullptr;
  }
  const RE::NiPointer<RE::NiAVObject> cloned{a_original->Clone()};
  auto *clone = cloned ? cloned->AsGeometry() : nullptr;
  if (!clone) {
    logger::warn("shell: Clone() of '{}' returned {}", a_original->name.c_str(),
                 cloned ? "a non-geometry" : "null");
    return nullptr;
  }
  std::unique_ptr<ShellBinding> shell{new ShellBinding{}};
  shell->clone_ = RE::NiPointer<RE::BSGeometry>{clone};
  clone->name = RE::BSFixedString{
      std::string{a_original->name.c_str() ? a_original->name.c_str() : ""} +
      Identity::ShellNodeSuffix()};

  auto &rt = clone->GetGeometryRuntimeData();
  auto *property =
      rt.properties[RE::BSGeometry::States::kEffect]
          ? netimmerse_cast<RE::BSLightingShaderProperty *>(
                rt.properties[RE::BSGeometry::States::kEffect].get())
          : nullptr;
  if (!property || property == a_property) {
    logger::warn("shell: clone of '{}' {} lighting property; shell dropped",
                 a_original->name.c_str(), property ? "shares its" : "has no");
    return nullptr;
  }
  shell->property_ = RE::NiPointer<RE::BSLightingShaderProperty>{property};
  const auto &originalRt = a_original->GetGeometryRuntimeData();
  const bool sharedSkin =
      rt.skinInstance.get() == originalRt.skinInstance.get();
  const bool sharedBuffers = rt.rendererData == originalRt.rendererData;

  auto *before = property->material;
  if (a_settings.material == ShellMaterial::kPbrCopy) {
    auto *original = a_property->material;
    const RE::BSTSmartPointer<RE::BSShaderMaterial> copy{original->Create()};
    if (!copy) {
      logger::warn("shell: could not copy the PBR material of '{}'",
                   a_original->name.c_str());
      return nullptr;
    }
    copy->CopyMembers(original);
    property->SetMaterial(copy.get(), true);
    if (!property->material || property->material == before ||
        property->material == original) {
      logger::warn("shell: SetMaterial on the clone of '{}' did not install "
                   "the PBR copy; shell dropped",
                   a_original->name.c_str());
      return nullptr;
    }
    property->flags.set(Flag::kVertexLighting);
    property->flags.reset(Flag::kRimLighting);
    const std::optional<PbrMaterial> material = PbrMaterial::Bind(property);
    if (!material) {
      logger::warn("shell: copied material failed PBR validation");
      return nullptr;
    }
    shell->slots_.emplace(*material);
  } else {
    const RE::BSTSmartPointer<RE::BSLightingShaderMaterialBase> vanilla{
        RE::BSLightingShaderMaterialBase::CreateMaterial(
            RE::BSShaderMaterial::Feature::kDefault)};
    const auto *base = static_cast<const RE::BSLightingShaderMaterialBase *>(
        a_property->material);
    if (!vanilla) {
      logger::warn("shell: CreateMaterial(kDefault) failed");
      return nullptr;
    }
    const auto *state = RE::BSGraphics::State::GetSingleton();
    auto *white = state && state->defaultTextureWhite
                      ? netimmerse_cast<RE::NiSourceTexture *>(
                            state->defaultTextureWhite.get())
                      : nullptr;
    vanilla->diffuseTexture = white ? RE::NiPointer<RE::NiSourceTexture>{white}
                                    : base->diffuseTexture;
    vanilla->normalTexture = base->normalTexture;
    vanilla->textureSet = base->textureSet;
    vanilla->textureClampMode = base->textureClampMode;
    vanilla->materialAlpha = 1.0f;
    vanilla->specularColor = RE::NiColor{0.0f, 0.0f, 0.0f};
    vanilla->specularColorScale = 0.0f;
    vanilla->specularPower = 30.0f;
    vanilla->subSurfaceLightRolloff = 0.3f;
    property->SetMaterial(vanilla.get(), true);
    if (!property->material || property->material == before ||
        property->material == a_property->material) {
      logger::warn("shell: SetMaterial on the clone of '{}' did not install "
                   "the vanilla material; shell dropped",
                   a_original->name.c_str());
      return nullptr;
    }
    shell->vanilla_ =
        static_cast<RE::BSLightingShaderMaterialBase *>(property->material);
    property->flags.reset(Flag::kVertexLighting);
    property->flags.set(Flag::kRimLighting);
    property->flags.set(Flag::kOwnEmit);
  }

  shell->materialOwner_ =
      RE::BSTSmartPointer<RE::BSShaderMaterial>{property->material};

  if (a_settings.depthBias) {
    property->flags.set(Flag::kDecal);
  } else {
    property->flags.reset(Flag::kDecal);
  }
  property->flags.set(Flag::kZBufferTest);
  property->flags.reset(Flag::kZBufferWrite);
  property->flags.reset(Flag::kSoftLighting);
  property->flags.reset(Flag::kBackLighting);

  bool ownEmissive = false;
  if (!property->emissiveColor ||
      property->emissiveColor == a_property->emissiveColor) {
    auto *color = RE::malloc<RE::NiColor>();
    if (!color) {
      return nullptr;
    }
    *color = RE::NiColor{0.0f, 0.0f, 0.0f};
    property->emissiveColor = color;
    ownEmissive = true;
  }

  shell->alpha_ = CreateAlphaProperty(a_settings.blend == ShellBlend::kAdditive,
                                      a_settings.alphaTest);
  if (!shell->alpha_) {
    return nullptr;
  }
  rt.properties[RE::BSGeometry::States::kProperty] = shell->alpha_;

  std::string inflation = "none (not skinned)";
  if (auto *skin = rt.skinInstance.get(); skin && skin->skinData &&
                                          skin->skinData->boneData &&
                                          skin->skinData->bones > 0) {
    const auto *originalSkin = originalRt.skinInstance.get();
    if (skin == originalSkin) {
      inflation = "none (skin instance shared)";
    } else {
      if (originalSkin &&
          skin->skinData.get() == originalSkin->skinData.get()) {
        auto copy = CopySkinData(*skin->skinData);
        if (copy) {
          skin->skinData = copy;
          inflation = "own skin data (copied)";
        } else {
          inflation = "none (skin data copy failed)";
        }
      } else {
        inflation = "own skin data (cloned)";
      }
      if (inflation.starts_with("own")) {
        shell->skinData_ = skin->skinData;
        shell->restSkinToBone_.assign(shell->skinData_->boneData,
                                      shell->skinData_->boneData +
                                          shell->skinData_->bones);
        for (auto &bone : shell->restSkinToBone_) {
          bone.boneVertData = nullptr;
        }
      }
    }
  }

  property->SetupGeometry(clone);
  property->FinishSetupGeometry(clone);
  property->DoClearRenderPasses();
  parent->AttachChild(clone, true);
  shell->parent_ = RE::NiPointer<RE::NiNode>{parent};
  RE::NiUpdateData data{};
  clone->Update(data);
  shell->description_ = std::format(
      "shell ({}, {}, skin {}, buffers {}, emissive storage {}, inflation {})",
      a_settings.material == ShellMaterial::kPbrCopy ? "PBR copy" : "vanilla",
      a_settings.blend == ShellBlend::kAdditive ? "additive" : "alpha",
      sharedSkin ? "shared" : "cloned", sharedBuffers ? "shared" : "cloned",
      ownEmissive ? "own" : "cloned", inflation);
  return shell;
}

ShellBinding::~ShellBinding() { Detach(); }

void ShellBinding::Detach() {
  if (parent_ && clone_) {
    parent_->DetachChild(clone_.get());
  }
  vanilla_ = nullptr;
  slots_.reset();
  materialOwner_.reset();
  skinData_.reset();
  restSkinToBone_.clear();
  alpha_.reset();
  property_.reset();
  clone_.reset();
  parent_.reset();
}

RE::BSGeometry *ShellBinding::Geometry() const noexcept { return clone_.get(); }

RE::BSLightingShaderProperty *ShellBinding::Property() const noexcept {
  return property_.get();
}

const std::string &ShellBinding::Describe() const noexcept {
  return description_;
}

bool ShellBinding::StillOwned() const noexcept {
  const auto *property = property_.get();
  if (!property || !clone_ || clone_->parent != parent_.get() ||
      clone_->GetGeometryRuntimeData()
              .properties[RE::BSGeometry::States::kEffect]
              .get() != property) {
    return false;
  }
  if (slots_) {
    return slots_->StillOwned();
  }
  return vanilla_ && property->material == vanilla_;
}

std::string ShellBinding::Problem(Slot a_slot) const {
  if (slots_) {
    return slots_->Problem(a_slot);
  }
  if (a_slot != Slot::kEmissive) {
    return std::format("a vanilla shell material has no '{}' slot",
                       SlotName(a_slot));
  }
  return property_ && property_->emissiveColor
             ? std::string{}
             : "the shell has no emissive colour storage";
}

void ShellBinding::WriteTexture(Slot a_slot, RE::NiSourceTexture *a_texture) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteTexture(a_slot, a_texture);
  }
}

void ShellBinding::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteEmissive(a_color, a_multiplier);
    return;
  }
  auto *property = property_.get();
  if (!property || !property->emissiveColor) {
    return;
  }
  property->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
  *property->emissiveColor = ToNi(a_color);
  property->emissiveMult = a_multiplier;
}

void ShellBinding::WriteFuzz(const Vec3 &a_color, float a_weight) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteFuzz(a_color, a_weight);
  }
}

void ShellBinding::WriteHeightScale(float a_scale) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteHeightScale(a_scale);
  }
}

void ShellBinding::WriteGlint(float a_screenSpaceScale,
                              float a_logMicrofacetDensity,
                              float a_microfacetRoughness,
                              float a_densityRandomization, bool a_enabled) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteGlint(a_screenSpaceScale, a_logMicrofacetDensity,
                       a_microfacetRoughness, a_densityRandomization,
                       a_enabled);
  }
}

void ShellBinding::WriteCoat(float a_roughness, float a_level) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteCoat(a_roughness, a_level);
  }
}

void ShellBinding::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteSubsurface(a_color, a_thickness);
  }
}

std::vector<SlotState> ShellBinding::Slots() const {
  return slots_ ? slots_->Slots() : std::vector<SlotState>{};
}

void ShellBinding::Pose(const Vec3 &a_inflate, float a_alpha, float a_rimPower,
                        float a_emissive) {
  auto *property = property_.get();
  if (!StillOwned()) {
    return;
  }
  property->SetMaterialAlpha(std::clamp(a_alpha, 0.0f, 1.0f));
  if (vanilla_) {
    vanilla_->rimLightPower = a_rimPower;
    property->emissiveMult = a_emissive;
  }
  if (skinData_ && skinData_->boneData && !restSkinToBone_.empty() &&
      !(a_inflate == lastInflate_)) {
    lastInflate_ = a_inflate;
    const float axis[3]{1.0f + a_inflate.x, 1.0f + a_inflate.y,
                        1.0f + a_inflate.z};
    for (std::uint32_t i = 0;
         i < restSkinToBone_.size() && i < skinData_->bones; ++i) {
      const auto &rest = restSkinToBone_[i].skinToBone;
      auto &live = skinData_->boneData[i].skinToBone;
      live = rest;
      for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
          live.rotate.entry[row][col] = rest.rotate.entry[row][col] * axis[row];
        }
      }
      live.translate.x = rest.translate.x * axis[0];
      live.translate.y = rest.translate.y * axis[1];
      live.translate.z = rest.translate.z * axis[2];
    }
  }
}

void ShellBinding::SetVisible(bool a_visible) {
  if (clone_) {
    clone_->SetAppCulled(!a_visible);
  }
}
}
