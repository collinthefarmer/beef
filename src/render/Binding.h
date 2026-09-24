// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/ShellPose.h"
#include "render/SkinPalette.h"
#include "render/TextureRef.h"

#include "PCH.h"
#include "planners/BindingPlan.h"
#include "planners/OwnedState.h"
#include "recipe/Recipe.h"
#include "render/PBRMaterial.h"

#include <array>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct SlotState {
  Slot slot = Slot::kEmissive;
  std::string original;
  std::string written;
};

class SlotTarget {
public:
  virtual ~SlotTarget() = default;
  [[nodiscard]] virtual std::optional<Diagnostic>
  Problem(Slot a_slot) const = 0;
  virtual void WriteTexture(Slot a_slot, const TextureRef &a_texture) = 0;
  virtual void WriteEmissive(const Vec3 &a_color, float a_multiplier) = 0;
  virtual void WriteFuzz(const Vec3 &a_color, float a_weight) = 0;
  virtual void WriteHeightScale(float a_scale) = 0;
  virtual void WriteGlint(const GlintParameters &a_parameters) = 0;
  virtual void WriteCoat(float a_roughness, float a_level) = 0;
  virtual void WriteSubsurface(const Vec3 &a_color, float a_thickness) = 0;
  [[nodiscard]] virtual std::vector<SlotState> Slots() const = 0;
};

class SlotWriter {
public:
  explicit SlotWriter(PbrMaterial a_material);
  SlotWriter(const SlotWriter &) = delete;
  SlotWriter &operator=(const SlotWriter &) = delete;
  SlotWriter(SlotWriter &&) = default;
  SlotWriter &operator=(SlotWriter &&) = default;

  [[nodiscard]] std::optional<Diagnostic> Problem(Slot a_slot) const;
  void WriteTexture(Slot a_slot, const TextureRef &a_texture);
  void WriteEmissive(const Vec3 &a_color, float a_multiplier);
  void WriteFuzz(const Vec3 &a_color, float a_weight);
  void WriteHeightScale(float a_scale);
  void WriteGlint(const GlintParameters &a_parameters);
  void WriteCoat(float a_roughness, float a_level);
  void WriteSubsurface(const Vec3 &a_color, float a_thickness);

  [[nodiscard]] bool StillOwned() const noexcept;
  void Restore();
  [[nodiscard]] std::vector<SlotState> Slots() const;

private:
  struct GroupState {
    std::uintptr_t texture = 0;
    std::uintptr_t storage = 0;
    Vec3 color{};
    float scalar = 0.0f;
    float roughness = 0.0f;
    GlintParameters glint{};
    std::uint32_t flags = 0;
    bool operator==(const GroupState &) const = default;
  };
  struct Group {
    OwnedState<GroupState> state;
    TextureRef original;
    TextureRef written;
  };
  [[nodiscard]] bool MaterialAttached() const noexcept;
  [[nodiscard]] std::optional<std::string> ProblemMessage(Slot a_slot) const;
  [[nodiscard]] GroupState Capture(Slot a_slot) const;
  [[nodiscard]] Group *BeginWrite(Slot a_slot, bool a_enableFeature = false);
  void EndWrite(Slot a_slot);
  void RestoreGroup(Slot a_slot, const GroupState &a_state,
                    const TextureRef &a_originalTexture);
  void RetainPublishedTextures() noexcept;
  struct PublishedTexture {
    RE::BSTSmartPointer<PBRMaterialLayout> material;
    Slot slot;
    TextureRef texture;
  };
  static std::list<PublishedTexture> &RetiredTextures();
  friend void SweepRetiredMaterialTextures();
  void SetFeature(Slot a_slot, bool a_on);
  [[nodiscard]] bool HasGroup(Slot a_slot) const;

  PbrMaterial material_;
  std::uint64_t traceID_ = 0;
  std::array<std::optional<Group>, kSlotCount> groups_;
  std::list<PublishedTexture> published_;
};

void SweepRetiredMaterialTextures();

class MaterialBinding final : public SlotTarget {
public:
  [[nodiscard]] static std::unique_ptr<MaterialBinding>
  Install(RE::BSGeometry *a_geometry, RE::BSLightingShaderProperty *a_property,
          bool a_uniqueCopy);
  ~MaterialBinding() override;
  MaterialBinding(const MaterialBinding &) = delete;
  MaterialBinding &operator=(const MaterialBinding &) = delete;

  [[nodiscard]] RE::BSLightingShaderProperty *Property() const noexcept;
  [[nodiscard]] bool Private() const noexcept;

  [[nodiscard]] std::optional<Diagnostic> Problem(Slot a_slot) const override;
  void WriteTexture(Slot a_slot, const TextureRef &a_texture) override;
  void WriteEmissive(const Vec3 &a_color, float a_multiplier) override;
  void WriteFuzz(const Vec3 &a_color, float a_weight) override;
  void WriteHeightScale(float a_scale) override;
  void WriteGlint(const GlintParameters &a_parameters) override;
  void WriteCoat(float a_roughness, float a_level) override;
  void WriteSubsurface(const Vec3 &a_color, float a_thickness) override;
  [[nodiscard]] std::vector<SlotState> Slots() const override;

  [[nodiscard]] bool StillOwned() const noexcept;

private:
  MaterialBinding() = default;

  RE::NiPointer<RE::BSGeometry> geometry_;
  RE::NiPointer<RE::BSLightingShaderProperty> property_;
  bool privateMaterial_ = false;
  std::optional<SlotWriter> slots_;
};

class ShellBinding final : public SlotTarget {
public:
  [[nodiscard]] static std::unique_ptr<ShellBinding>
  Create(RE::BSGeometry *a_original, RE::BSLightingShaderProperty *a_property,
         const ShellSettings &a_settings);
  ~ShellBinding() override;
  ShellBinding(const ShellBinding &) = delete;
  ShellBinding &operator=(const ShellBinding &) = delete;

  [[nodiscard]] RE::BSGeometry *Geometry() const noexcept;
  [[nodiscard]] RE::BSLightingShaderProperty *Property() const noexcept;
  [[nodiscard]] const std::string &Describe() const noexcept;

  [[nodiscard]] std::optional<Diagnostic> Problem(Slot a_slot) const override;
  void WriteTexture(Slot a_slot, const TextureRef &a_texture) override;
  void WriteEmissive(const Vec3 &a_color, float a_multiplier) override;
  void WriteFuzz(const Vec3 &a_color, float a_weight) override;
  void WriteHeightScale(float a_scale) override;
  void WriteGlint(const GlintParameters &a_parameters) override;
  void WriteCoat(float a_roughness, float a_level) override;
  void WriteSubsurface(const Vec3 &a_color, float a_thickness) override;
  [[nodiscard]] std::vector<SlotState> Slots() const override;

  void Pose(const ShellPoseValues &a_pose, float a_alpha, float a_rimPower,
            float a_emissive);
  void SetVisible(bool a_visible);
  [[nodiscard]] bool StillOwned() const noexcept;

private:
  ShellBinding() = default;
  struct Builder;
  void Detach();

  RE::NiPointer<RE::BSGeometry> clone_;
  std::unique_ptr<SkinPaletteLease> palette_;
  RE::NiPointer<RE::NiNode> parent_;
  RE::NiPointer<RE::BSLightingShaderProperty> property_;
  RE::NiPointer<RE::NiAlphaProperty> alpha_;
  RE::NiPointer<RE::NiSkinData> skinData_;
  std::vector<RE::NiSkinData::BoneData> restSkinToBone_;
  RE::BSTSmartPointer<RE::BSShaderMaterial> materialOwner_;
  RE::BSLightingShaderMaterialBase *vanilla_ = nullptr;
  std::optional<SlotWriter> slots_;
  bool tracedPose_ = false;
  std::uint8_t tracedPoseCalls_ = 0;
  ShellPoseValues lastPose_{.inflate{-1.0f, -1.0f, -1.0f},
                            .offset{0.0f, 0.0f, 0.0f},
                            .scale = 1.0f,
                            .scalePoint{0.0f, 0.0f, 0.0f},
                            .spin = 0.0f,
                            .spinAxis{0.0f, 0.0f, 1.0f}};
  std::string description_;
};

struct LightPlacement {
  RE::NiPointer<RE::NiNode> bone;
  std::string name;
  RE::NiPoint3 offset;
  float share = 1.0f;
};

[[nodiscard]] std::vector<LightPlacement>
PlaceLightNodes(const Bones &a_bones,
                std::span<RE::BSGeometry *const> a_geometries,
                RE::NiAVObject *a_root, const Vec3 &a_offset);

class LightBinding {
public:
  [[nodiscard]] static std::unique_ptr<LightBinding>
  Create(const std::vector<LightPlacement> &a_placements, bool a_shadow);
  ~LightBinding();
  LightBinding(const LightBinding &) = delete;
  LightBinding &operator=(const LightBinding &) = delete;

  void Update(const Vec3 &a_color, float a_intensity, float a_size,
              float a_cutoff, bool a_visible);
  [[nodiscard]] std::string Describe() const;

private:
  LightBinding() = default;

  struct Entry {
    RE::NiPointer<RE::NiPointLight> light;
    RE::NiPointer<RE::BSLight> bsLight;
    RE::NiPointer<RE::NiNode> bone;
    std::string name;
    float share = 1.0f;
  };
  std::vector<Entry> entries_;
  RE::NiPointer<RE::ShadowSceneNode> scene_;
  using RemoveLight = void (*)(RE::ShadowSceneNode *,
                               const RE::NiPointer<RE::BSLight> &);
  RemoveLight removeLight_ = nullptr;
  bool shadow_ = false;
};
}
