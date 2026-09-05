#pragma once

// Outputs to the engine: the only module that writes engine state. A
// binding records what it installed and puts the original back when it is
// destroyed, unless another system took the slot over, in which case it
// leaves it alone and says so.

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
	// One slot of a material as it stands: the map it had and the map written.
	struct SlotState
	{
		Slot        slot = Slot::kEmissive;
		std::string original;
		std::string written;
	};

	// What a recipe writes on a surface, per slot: its composite texture and
	// the scalars beside it. A material binding and a shell binding are both
	// targets, so the manager writes the same way to either.
	class SlotTarget
	{
	public:
		virtual ~SlotTarget() = default;
		// Empty when this slot can be written here; else why not.
		[[nodiscard]] virtual std::string Problem(Slot a_slot) const = 0;
		// The texture in the slot; null puts the original back.
		virtual void WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture) = 0;
		virtual void WriteEmissive(const Vec3& a_color, float a_multiplier) = 0;
		virtual void WriteFuzz(const Vec3& a_color, float a_weight) = 0;
		virtual void WriteHeightScale(float a_scale) = 0;
		// Glint has no map: parameters only, enabled while written.
		virtual void WriteGlint(float a_screenSpaceScale, float a_logMicrofacetDensity, float a_microfacetRoughness, float a_densityRandomization, bool a_enabled) = 0;
		// Coat: the map carries colour and strength; roughness and level are scalars.
		virtual void WriteCoat(float a_roughness, float a_level) = 0;
		// Subsurface: the map carries colour and thickness; these scale it.
		virtual void WriteSubsurface(const Vec3& a_color, float a_thickness) = 0;
		[[nodiscard]] virtual std::vector<SlotState> Slots() const = 0;
	};

	// The slot writes on one PBR material. The first write to a slot saves the
	// original; Restore puts every original back. Ownership is lost when
	// another system replaces a texture this writer wrote, and then nothing
	// is restored.
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
		// A feature's flag bits go on with its first write and off when its
		// output is hidden (a null texture), unless the material had them.
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
		std::optional<std::uint32_t>                        flags_;  // pbrFlags before the first feature write; restored last
		std::optional<SavedFuzz>                            fuzz_;
		std::optional<SavedGlint>                           glint_;
		std::optional<SavedCoat>                            coat_;
		std::optional<SavedSubsurface>                      subsurface_;
		std::optional<float>                                heightScale_;
	};

	// A geometry's own material, made private, with the slots the recipe binds.
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

		// False when another system replaced what this binding wrote.
		[[nodiscard]] bool StillOwned() const noexcept { return slots_.StillOwned(); }

	private:
		MaterialBinding() = default;

		RE::NiPointer<RE::BSGeometry>               geometry_;
		RE::NiPointer<RE::BSLightingShaderProperty> property_;
		PBRMaterialLayout*                          material_ = nullptr;
		RE::BSTSmartPointer<RE::BSShaderMaterial>   original_;  // set when a private copy was installed
		SlotWriter                                  slots_;
	};

	// A clone of the geometry with its own material (a PBR copy or a vanilla
	// lighting material), blended over the original, inflated per tick.
	class ShellBinding final : public SlotTarget
	{
	public:
		[[nodiscard]] static std::unique_ptr<ShellBinding> Create(RE::BSGeometry* a_original, RE::BSLightingShaderProperty* a_property, const ShellSettings& a_settings);
		~ShellBinding() override;
		ShellBinding(const ShellBinding&) = delete;
		ShellBinding& operator=(const ShellBinding&) = delete;

		[[nodiscard]] RE::BSGeometry*               Geometry() const noexcept { return clone_.get(); }
		[[nodiscard]] RE::BSLightingShaderProperty* Property() const noexcept { return property_.get(); }
		// The PBR copy's layout when the shell material is a PBR copy; null for vanilla.
		[[nodiscard]] PBRMaterialLayout*            PbrMaterial() const noexcept { return pbr_; }
		[[nodiscard]] const std::string&            Describe() const noexcept { return description_; }

		// A vanilla shell material has the emissive slot only.
		[[nodiscard]] std::string            Problem(Slot a_slot) const override;
		void                                 WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture) override;
		void                                 WriteEmissive(const Vec3& a_color, float a_multiplier) override;
		void                                 WriteFuzz(const Vec3& a_color, float a_weight) override;
		void                                 WriteHeightScale(float a_scale) override;
		void                                 WriteGlint(float a_scale, float a_density, float a_roughness, float a_randomization, bool a_enabled) override;
		void                                 WriteCoat(float a_roughness, float a_level) override;
		void                                 WriteSubsurface(const Vec3& a_color, float a_thickness) override;
		[[nodiscard]] std::vector<SlotState> Slots() const override;

		// Per tick: inflation per bone-space axis, material alpha, and for a
		// vanilla material its rim power and emissive multiplier.
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
		SlotWriter                                  slots_;  // over pbr_ when the shell is a PBR copy
		Vec3                                        lastInflate_{ -1.0f, -1.0f, -1.0f };
		std::string                                 description_;
	};

	// Where a light goes: a skeleton node, an offset in its space, a share of
	// the intensity.
	struct LightPlacement
	{
		RE::NiPointer<RE::NiNode> bone;
		std::string               name;
		RE::NiPoint3              offset;
		float                     share = 1.0f;
	};

	// Bones by skinned vertex share across the recipe's geometries, or by name.
	[[nodiscard]] std::vector<LightPlacement> PlaceLights(const Bones& a_bones, std::span<RE::BSGeometry* const> a_geometries, RE::NiAVObject* a_root, const Vec3& a_offset);

	// Point lights registered with the shadow scene node, inverse-square
	// under CS's ISL through the NiLight runtime overlay.
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

	// Geometry names ending in this are shells the plugin attached; the apply
	// traversal skips them.
	[[nodiscard]] std::string ShellSuffix();
}
