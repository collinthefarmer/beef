#include "render/Binding.h"

#include "Identity.h"
#include "diagnostics/Trace.h"
#include "render/SkinData.h"

#include <algorithm>
#include <cstring>

namespace BetterEnchantmentEffects {
namespace {
std::string TransformText(const RE::NiTransform &a_transform) {
  std::string text;
  for (const auto &row : a_transform.rotate.entry) {
    for (const float value : row) {
      text += std::format("{},", value);
    }
  }
  return text + std::format("{},{},{},{}", a_transform.translate.x,
                            a_transform.translate.y, a_transform.translate.z,
                            a_transform.scale);
}

void TraceSkin(std::string_view a_role, const RE::NiSkinInstance *a_skin) {
  if (!a_skin || !Trace::Get().Inspect().enabled) {
    return;
  }
  const std::uint32_t total = a_skin->skinData ? a_skin->skinData->bones : 0;
  const auto count = std::min<std::uint32_t>(total, 256);
  Trace::Safely([&] {
    Trace::Emit(
        Trace::Event::kShell,
        {{"action", "skin"},
         {"role", std::string{a_role}},
         {"bone_count", std::to_string(total)},
         {"sampled_bones",
          std::to_string(a_skin->skinData && a_skin->skinData->boneData ? count
                                                                        : 0)},
         {"palette_limited", total > count ? "true" : "false"},
         {"skin", Trace::Pointer(a_skin)},
         {"data", Trace::Pointer(a_skin->skinData.get())},
         {"partition", Trace::Pointer(a_skin->skinPartition.get())},
         {"root", Trace::Pointer(a_skin->rootParent)},
         {"bones", Trace::Pointer(static_cast<const void *>(a_skin->bones))},
         {"world_transforms", Trace::Pointer(static_cast<const void *>(
                                  a_skin->boneWorldTransforms))},
         {"matrices", Trace::Pointer(a_skin->boneMatrices)},
         {"previous_matrices", Trace::Pointer(a_skin->prevBoneMatrices)},
         {"frame", std::to_string(a_skin->frameID)},
         {"matrix_count", std::to_string(a_skin->numMatrices)}});
  });
  if (!a_skin->skinData || !a_skin->skinData->boneData) {
    return;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    Trace::Safely([&] {
      Trace::Emit(
          Trace::Event::kShell,
          {{"action", "bone"},
           {"role", std::string{a_role}},
           {"skin", Trace::Pointer(a_skin)},
           {"index", std::to_string(i)},
           {"total", std::to_string(a_skin->skinData->bones)},
           {"node", Trace::Pointer(a_skin->bones ? a_skin->bones[i] : nullptr)},
           {"world_transform",
            Trace::Pointer(a_skin->boneWorldTransforms
                               ? a_skin->boneWorldTransforms[i]
                               : nullptr)},
           {"bind_transform",
            TransformText(a_skin->skinData->boneData[i].skinToBone)}});
    });
  }
}

void TraceShellState(std::string_view a_role, RE::BSGeometry *a_geometry) {
  Trace::Safely([&] {
    if (!a_geometry || !Trace::Get().Inspect().enabled) {
      return;
    }
    const auto &runtime = a_geometry->GetGeometryRuntimeData();
    Trace::Emit(Trace::Event::kShell,
                {{"action", "state"},
                 {"role", std::string{a_role}},
                 {"geometry", Trace::Pointer(a_geometry)},
                 {"skin", Trace::Pointer(runtime.skinInstance.get())},
                 {"parent", Trace::Pointer(a_geometry->parent)},
                 {"local", TransformText(a_geometry->local)},
                 {"world", TransformText(a_geometry->world)}});
    TraceSkin(a_role, runtime.skinInstance.get());
  });
}

RE::NiTransform InflatedTransform(const RE::NiTransform &rest,
                                  const Vec3 &inflate) {
  const float axis[3]{1.0f + inflate.x, 1.0f + inflate.y, 1.0f + inflate.z};
  RE::NiTransform live = rest;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      live.rotate.entry[row][col] = rest.rotate.entry[row][col] * axis[row];
    }
  }
  live.translate.x = rest.translate.x * axis[0];
  live.translate.y = rest.translate.y * axis[1];
  live.translate.z = rest.translate.z * axis[2];
  return live;
}

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

}

struct ShellBinding::Builder {
  enum class EmissiveStorage : std::uint8_t { kCloned, kAllocated };
  ShellBinding &shell;
  RE::BSGeometry *original;
  RE::BSLightingShaderProperty *sourceProperty;
  const ShellSettings &settings;

  [[nodiscard]] RE::BSGeometry *Clone() const { return shell.clone_.get(); }
  [[nodiscard]] RE::BSLightingShaderProperty *Property() const {
    return shell.property_.get();
  }

  bool PrepareCloneProperty() {
    auto *clone = Clone();
    clone->name = RE::BSFixedString{
        std::string{original->name.c_str() ? original->name.c_str() : ""} +
        Identity::ShellNodeSuffix()};

    auto &rt = clone->GetGeometryRuntimeData();
    auto *property =
        rt.properties[RE::BSGeometry::States::kEffect]
            ? netimmerse_cast<RE::BSLightingShaderProperty *>(
                  rt.properties[RE::BSGeometry::States::kEffect].get())
            : nullptr;
    if (!property || property == sourceProperty) {
      logger::warn("shell: clone of '{}' {} lighting property; shell dropped",
                   original->name.c_str(), property ? "shares its" : "has no");
      return false;
    }
    shell.property_ = RE::NiPointer<RE::BSLightingShaderProperty>{property};
    return true;
  }

  void TraceClone() const {
    auto *clone = Clone();
    auto *property = Property();
    auto *parent = original->parent;
    const auto &rt = clone->GetGeometryRuntimeData();
    const auto &originalRt = original->GetGeometryRuntimeData();
    Trace::Safely([&] {
      Trace::Emit(Trace::Event::kShell,
                  {{"action", "cloned"},
                   {"source", Trace::Pointer(original)},
                   {"clone", Trace::Pointer(clone)},
                   {"source_property", Trace::Pointer(sourceProperty)},
                   {"clone_property", Trace::Pointer(property)},
                   {"parent", Trace::Pointer(parent)},
                   {"source_buffers", Trace::Pointer(originalRt.rendererData)},
                   {"clone_buffers", Trace::Pointer(rt.rendererData)},
                   {"source_local", TransformText(original->local)},
                   {"clone_local", TransformText(clone->local)},
                   {"source_world", TransformText(original->world)},
                   {"clone_world", TransformText(clone->world)}});
    });
    TraceSkin("source", originalRt.skinInstance.get());
    TraceSkin("clone_before_copy", rt.skinInstance.get());
  }

  bool InstallMaterial(RE::BSShaderMaterial &material,
                       std::string_view description) {
    auto *property = Property();
    auto *before = property->material;
    property->SetMaterial(&material, true);
    if (!property->material || property->material == before ||
        property->material == sourceProperty->material) {
      logger::warn("shell: SetMaterial on the clone of '{}' did not install {}"
                   "; shell dropped",
                   original->name.c_str(), description);
      return false;
    }
    return true;
  }

  bool CopyPbrMaterial() {
    using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
    auto *property = Property();
    auto *sourceMaterial = sourceProperty->material;
    const RE::BSTSmartPointer<RE::BSShaderMaterial> copy{
        sourceMaterial->Create()};
    if (!copy) {
      logger::warn("shell: could not copy the PBR material of '{}'",
                   original->name.c_str());
      return false;
    }
    copy->CopyMembers(sourceMaterial);
    if (!InstallMaterial(*copy, "the PBR copy")) {
      return false;
    }
    property->flags.set(Flag::kVertexLighting);
    property->flags.reset(Flag::kRimLighting);
    const std::optional<PbrMaterial> material = PbrMaterial::Bind(property);
    if (!material) {
      logger::warn("shell: copied material failed PBR validation");
      return false;
    }
    shell.slots_.emplace(*material);
    return true;
  }

  bool CreateVanillaMaterial() {
    using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
    auto *property = Property();
    const RE::BSTSmartPointer<RE::BSLightingShaderMaterialBase> vanilla{
        RE::BSLightingShaderMaterialBase::CreateMaterial(
            RE::BSShaderMaterial::Feature::kDefault)};
    const auto *base = static_cast<const RE::BSLightingShaderMaterialBase *>(
        sourceProperty->material);
    if (!vanilla) {
      logger::warn("shell: CreateMaterial(kDefault) failed");
      return false;
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
    if (!InstallMaterial(*vanilla, "the vanilla material")) {
      return false;
    }
    shell.vanilla_ =
        static_cast<RE::BSLightingShaderMaterialBase *>(property->material);
    property->flags.reset(Flag::kVertexLighting);
    property->flags.set(Flag::kRimLighting);
    property->flags.set(Flag::kOwnEmit);
    return true;
  }

  std::optional<EmissiveStorage> ConfigureProperty() {
    using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
    auto *property = Property();
    if (settings.depthBias) {
      property->flags.set(Flag::kDecal);
    } else {
      property->flags.reset(Flag::kDecal);
    }
    property->flags.set(Flag::kZBufferTest);
    property->flags.reset(Flag::kZBufferWrite);
    property->flags.reset(Flag::kSoftLighting);
    property->flags.reset(Flag::kBackLighting);

    EmissiveStorage emissiveStorage = EmissiveStorage::kCloned;
    if (!property->emissiveColor ||
        property->emissiveColor == sourceProperty->emissiveColor) {
      auto *color = RE::malloc<RE::NiColor>();
      if (!color) {
        return std::nullopt;
      }
      *color = RE::NiColor{0.0f, 0.0f, 0.0f};
      property->emissiveColor = color;
      emissiveStorage = EmissiveStorage::kAllocated;
    }

    return emissiveStorage;
  }

  std::string PrepareInflation() {
    auto *skin = Clone()->GetGeometryRuntimeData().skinInstance.get();
    if (!skin || !skin->skinData || !skin->skinData->boneData ||
        skin->skinData->bones == 0) {
      return "none (not skinned)";
    }
    const auto *originalSkin =
        original->GetGeometryRuntimeData().skinInstance.get();
    if (skin == originalSkin) {
      return "none (skin instance shared)";
    }
    const bool sharedData =
        originalSkin && skin->skinData.get() == originalSkin->skinData.get();
    if (sharedData) {
      auto copy = CopySkinData(*skin->skinData);
      if (!copy) {
        return "none (skin data copy failed)";
      }
      skin->skinData = copy;
    }
    shell.skinData_ = skin->skinData;
    shell.restSkinToBone_.assign(shell.skinData_->boneData,
                                 shell.skinData_->boneData +
                                     shell.skinData_->bones);
    for (auto &bone : shell.restSkinToBone_) {
      bone.boneVertData = nullptr;
    }
    return sharedData ? "own skin data (copied)" : "own skin data (cloned)";
  }

  bool Attach() {
    auto *clone = Clone();
    auto *property = Property();
    auto *parent = original->parent;
    auto &rt = clone->GetGeometryRuntimeData();
    TraceSkin("clone_after_copy", rt.skinInstance.get());
    shell.palette_ = SkinPaletteLease::Preserve(*original, *clone);
    if (!shell.palette_) {
      return false;
    }
    property->SetupGeometry(clone);
    property->FinishSetupGeometry(clone);
    property->DoClearRenderPasses();
    parent->AttachChild(clone, true);
    shell.parent_ = RE::NiPointer<RE::NiNode>{parent};
    RE::NiUpdateData data{};
    clone->Update(data);
    TraceShellState("source_after_attach", original);
    TraceShellState("clone_after_attach", clone);
    return true;
  }

  bool Build() {
    if (!PrepareCloneProperty()) {
      return false;
    }
    TraceClone();
    auto &rt = Clone()->GetGeometryRuntimeData();
    const auto &originalRt = original->GetGeometryRuntimeData();
    const bool sharedSkin =
        rt.skinInstance.get() == originalRt.skinInstance.get();
    const bool sharedBuffers = rt.rendererData == originalRt.rendererData;
    const bool materialReady = settings.material == ShellMaterial::kPbrCopy
                                   ? CopyPbrMaterial()
                                   : CreateVanillaMaterial();
    if (!materialReady) {
      return false;
    }
    shell.materialOwner_ =
        RE::BSTSmartPointer<RE::BSShaderMaterial>{Property()->material};
    const auto emissiveStorage = ConfigureProperty();
    if (!emissiveStorage.has_value()) {
      return false;
    }
    shell.alpha_ = CreateAlphaProperty(settings.blend == ShellBlend::kAdditive,
                                       settings.alphaTest);
    if (!shell.alpha_) {
      return false;
    }
    rt.properties[RE::BSGeometry::States::kProperty] = shell.alpha_;
    const std::string inflation = PrepareInflation();
    if (!Attach()) {
      return false;
    }
    shell.description_ = std::format(
        "shell ({}, {}, skin {}, buffers {}, emissive storage {}, inflation "
        "{})",
        settings.material == ShellMaterial::kPbrCopy ? "PBR copy" : "vanilla",
        settings.blend == ShellBlend::kAdditive ? "additive" : "alpha",
        sharedSkin ? "shared" : "cloned", sharedBuffers ? "shared" : "cloned",
        *emissiveStorage == EmissiveStorage::kAllocated ? "own" : "cloned",
        inflation);
    return true;
  }
};

std::unique_ptr<ShellBinding>
ShellBinding::Create(RE::BSGeometry *a_original,
                     RE::BSLightingShaderProperty *a_property,
                     const ShellSettings &a_settings) {
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
  if (!Builder{*shell, a_original, a_property, a_settings}.Build()) {
    return nullptr;
  }
  return shell;
}

ShellBinding::~ShellBinding() { Detach(); }

void ShellBinding::Detach() {
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kShell,
                {{"action", "detach"},
                 {"clone", Trace::Pointer(clone_.get())},
                 {"parent", Trace::Pointer(parent_.get())}});
  });
  if (parent_ && clone_) {
    parent_->DetachChild(clone_.get());
  }
  palette_.reset();
  vanilla_ = nullptr;
  if (slots_)
    slots_->Restore();
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
  if (!property || !clone_ || !palette_ || !palette_->StillOwned(*clone_) ||
      clone_->parent != parent_.get() ||
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

void ShellBinding::WriteTexture(Slot a_slot, const TextureRef &a_texture) {
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

void ShellBinding::WriteGlint(const GlintParameters &a_parameters) {
  if (!StillOwned()) {
    return;
  }
  if (slots_) {
    slots_->WriteGlint(a_parameters);
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
  if (!tracedPose_) {
    TraceShellState("clone_before_first_pose", clone_.get());
    tracedPose_ = true;
    Trace::Safely([&] {
      Trace::Emit(Trace::Event::kShell,
                  {{"action", "first_pose"},
                   {"clone", Trace::Pointer(clone_.get())},
                   {"skin_data", Trace::Pointer(skinData_.get())},
                   {"inflate", std::format("{},{},{}", a_inflate.x, a_inflate.y,
                                           a_inflate.z)},
                   {"alpha", std::to_string(a_alpha)},
                   {"emissive", std::to_string(a_emissive)}});
    });
  }
  property->SetMaterialAlpha(std::clamp(a_alpha, 0.0f, 1.0f));
  if (vanilla_) {
    vanilla_->rimLightPower = a_rimPower;
    property->emissiveMult = a_emissive;
  }
  if (skinData_ && skinData_->boneData && !restSkinToBone_.empty() &&
      !(a_inflate == lastInflate_)) {
    lastInflate_ = a_inflate;
    for (std::uint32_t i = 0;
         i < restSkinToBone_.size() && i < skinData_->bones; ++i) {
      skinData_->boneData[i].skinToBone =
          InflatedTransform(restSkinToBone_[i].skinToBone, a_inflate);
    }
  }
  if (tracedPoseCalls_ < 30) {
    ++tracedPoseCalls_;
    if (tracedPoseCalls_ == 1) {
      TraceShellState("clone_after_first_pose", clone_.get());
    } else if (tracedPoseCalls_ == 2) {
      TraceShellState("clone_after_second_pose", clone_.get());
    } else if (tracedPoseCalls_ == 30) {
      TraceShellState("clone_after_thirtieth_pose", clone_.get());
    }
  }
}

void ShellBinding::SetVisible(bool a_visible) {
  if (clone_) {
    clone_->SetAppCulled(!a_visible);
  }
}
}
