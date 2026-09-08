#pragma once

#include "PBRMaterial.h"
#include "PCH.h"
#include "Recipe.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace WornEnchantmentPBR
{
	struct SlotState
	{
		Slot        slot = Slot::kEmissive;
		std::string original;
		std::string written;
	};

	class SlotTarget
	{
	public:
		virtual ~SlotTarget() = default;
		[[nodiscard]] virtual std::string Problem(Slot a_slot) const = 0;
		virtual void WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture) = 0;
		virtual void WriteEmissive(const Vec3& a_color, float a_multiplier) = 0;
		virtual void WriteFuzz(const Vec3& a_color, float a_weight) = 0;
		virtual void WriteHeightScale(float a_scale) = 0;
		virtual void WriteGlint(float a_screenSpaceScale, float a_logMicrofacetDensity, float a_microfacetRoughness, float a_densityRandomization, bool a_enabled) = 0;
		virtual void WriteCoat(float a_roughness, float a_level) = 0;
		virtual void WriteSubsurface(const Vec3& a_color, float a_thickness) = 0;
		[[nodiscard]] virtual std::vector<SlotState> Slots() const = 0;
	};

	class SlotWriter
	{
	public:
		SlotWriter() = default;
		SlotWriter(PBRMaterialLayout* a_material, RE::BSLightingShaderProperty* a_property);

		[[nodiscard]] std::string Problem(Slot a_slot) const;
		void                      WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture);
		void                      WriteEmissive(const Vec3& a_color, float a_multiplier);
		void                      WriteFuzz(const Vec3& a_color, float a_weight);
		void                      WriteHeightScale(float a_scale);
		void                      WriteGlint(float a_screenSpaceScale, float a_logMicrofacetDensity, float a_microfacetRoughness, float a_densityRandomization, bool a_enabled);
		void                      WriteCoat(float a_roughness, float a_level);
		void                      WriteSubsurface(const Vec3& a_color, float a_thickness);

		[[nodiscard]] bool                   StillOwned() const noexcept;
		void                                 Restore();
		[[nodiscard]] std::vector<SlotState> Slots() const;

	private:
		void SetFeature(std::uint32_t a_bits, bool a_on);
		void EnableFuzz();
		void EnableCoat();
		void EnableSubsurface();

		struct SavedTexture
		{
			RE::NiPointer<RE::NiSourceTexture> original;
			RE::NiSourceTexture*               written = nullptr;
		};
		struct SavedEmissive
		{
			RE::NiColor color;
			float       multiplier = 0.0f;
			bool        ownEmit = false;
		};
		struct SavedFuzz
		{
			RE::NiColor color;
			float       weight = 0.0f;
		};
		struct SavedGlint
		{
			GlintParameters parameters;
		};
		struct SavedCoat
		{
			float roughness = 1.0f;
			float level = 0.04f;
		};
		struct SavedSubsurface
		{
			RE::NiColor color;
			float       rolloff = 0.0f;
		};

		PBRMaterialLayout*                                  material_ = nullptr;
		RE::BSLightingShaderProperty*                       property_ = nullptr;
		std::array<std::optional<SavedTexture>, kSlotCount> textures_;
		std::optional<SavedEmissive>                        emissive_;
		std::optional<std::uint32_t>                        flags_;
		std::optional<SavedFuzz>                            fuzz_;
		std::optional<SavedGlint>                           glint_;
		std::optional<SavedCoat>                            coat_;
		std::optional<SavedSubsurface>                      subsurface_;
		std::optional<float>                                heightScale_;
	};

	class MaterialBinding final : public SlotTarget
	{
	public:
		[[nodiscard]] static std::unique_ptr<MaterialBinding> Install(RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property, bool a_uniqueCopy);
		~MaterialBinding() override;
		MaterialBinding(const MaterialBinding&) = delete;
		MaterialBinding& operator=(const MaterialBinding&) = delete;

		[[nodiscard]] PBRMaterialLayout*            Material() const noexcept { return material_; }
		[[nodiscard]] RE::BSLightingShaderProperty* Property() const noexcept { return property_.get(); }
		[[nodiscard]] bool                          Private() const noexcept { return static_cast<bool>(original_); }

		[[nodiscard]] std::string            Problem(Slot a_slot) const override { return slots_.Problem(a_slot); }
		void                                 WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture) override { slots_.WriteTexture(a_slot, a_texture); }
		void                                 WriteEmissive(const Vec3& a_color, float a_multiplier) override { slots_.WriteEmissive(a_color, a_multiplier); }
		void                                 WriteFuzz(const Vec3& a_color, float a_weight) override { slots_.WriteFuzz(a_color, a_weight); }
		void                                 WriteHeightScale(float a_scale) override { slots_.WriteHeightScale(a_scale); }
		void                                 WriteGlint(float a_scale, float a_density, float a_roughness, float a_randomization, bool a_enabled) override { slots_.WriteGlint(a_scale, a_density, a_roughness, a_randomization, a_enabled); }
		void                                 WriteCoat(float a_roughness, float a_level) override { slots_.WriteCoat(a_roughness, a_level); }
		void                                 WriteSubsurface(const Vec3& a_color, float a_thickness) override { slots_.WriteSubsurface(a_color, a_thickness); }
		[[nodiscard]] std::vector<SlotState> Slots() const override { return slots_.Slots(); }

		[[nodiscard]] bool StillOwned() const noexcept { return slots_.StillOwned(); }

	private:
		MaterialBinding() = default;

		RE::NiPointer<RE::BSGeometry>               geometry_;
		RE::NiPointer<RE::BSLightingShaderProperty> property_;
		PBRMaterialLayout*                          material_ = nullptr;
		RE::BSTSmartPointer<RE::BSShaderMaterial>   original_;
		SlotWriter                                  slots_;
	};

	class ShellBinding final : public SlotTarget
	{
	public:
		[[nodiscard]] static std::unique_ptr<ShellBinding> Create(RE::BSGeometry* a_original, RE::BSLightingShaderProperty* a_property, const ShellSettings& a_settings);
		~ShellBinding() override;
		ShellBinding(const ShellBinding&) = delete;
		ShellBinding& operator=(const ShellBinding&) = delete;

		[[nodiscard]] RE::BSGeometry*               Geometry() const noexcept { return clone_.get(); }
		[[nodiscard]] RE::BSLightingShaderProperty* Property() const noexcept { return property_.get(); }
		[[nodiscard]] PBRMaterialLayout*            PbrMaterial() const noexcept { return pbr_; }
		[[nodiscard]] const std::string&            Describe() const noexcept { return description_; }

		[[nodiscard]] std::string            Problem(Slot a_slot) const override;
		void                                 WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture) override;
		void                                 WriteEmissive(const Vec3& a_color, float a_multiplier) override;
		void                                 WriteFuzz(const Vec3& a_color, float a_weight) override;
		void                                 WriteHeightScale(float a_scale) override;
		void                                 WriteGlint(float a_scale, float a_density, float a_roughness, float a_randomization, bool a_enabled) override;
		void                                 WriteCoat(float a_roughness, float a_level) override;
		void                                 WriteSubsurface(const Vec3& a_color, float a_thickness) override;
		[[nodiscard]] std::vector<SlotState> Slots() const override;

		void Pose(const Vec3& a_inflate, float a_alpha, float a_rimPower, float a_emissive);
		void SetVisible(bool a_visible);
		[[nodiscard]] bool StillOwned() const noexcept;

	private:
		ShellBinding() = default;
		void Detach();

		RE::NiPointer<RE::BSGeometry>               clone_;
		RE::NiPointer<RE::NiNode>                   parent_;
		RE::NiPointer<RE::BSLightingShaderProperty> property_;
		RE::NiPointer<RE::NiAlphaProperty>          alpha_;
		RE::NiPointer<RE::NiSkinData>               skinData_;
		std::vector<RE::NiSkinData::BoneData>       restSkinToBone_;
		PBRMaterialLayout*                          pbr_ = nullptr;
		RE::BSLightingShaderMaterialBase*           vanilla_ = nullptr;
		SlotWriter                                  slots_;
		Vec3                                        lastInflate_{ -1.0f, -1.0f, -1.0f };
		std::string                                 description_;
	};

	struct LightPlacement
	{
		RE::NiPointer<RE::NiNode> bone;
		std::string               name;
		RE::NiPoint3              offset;
		float                     share = 1.0f;
	};

	[[nodiscard]] std::vector<LightPlacement> PlaceLights(const Bones& a_bones, std::span<RE::BSGeometry* const> a_geometries, RE::NiAVObject* a_root, const Vec3& a_offset);

	class LightBinding
	{
	public:
		[[nodiscard]] static std::unique_ptr<LightBinding> Create(const std::vector<LightPlacement>& a_placements, bool a_shadow);
		~LightBinding();
		LightBinding(const LightBinding&) = delete;
		LightBinding& operator=(const LightBinding&) = delete;

		void                      Update(const Vec3& a_color, float a_intensity, float a_size, float a_cutoff, bool a_visible);
		[[nodiscard]] std::string Describe() const;

	private:
		LightBinding() = default;

		struct Entry
		{
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bsLight;
			RE::NiPointer<RE::NiNode>       bone;
			std::string                     name;
			float                           share = 1.0f;
		};
		std::vector<Entry> entries_;
		bool               shadow_ = false;
	};

	[[nodiscard]] std::string ShellSuffix();
}
