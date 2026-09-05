#include "Binding.h"

#include "Identity.h"

#include <bit>
#include <cstring>

namespace WornEnchantmentPBR
{
	namespace
	{
		// Address Library IDs (SE, AE) from powerof3's CommonLibSSE fork; the
		// pinned CommonLibSSE-NG does not wrap these (NOTES 29).
		constexpr REL::RelocationID kNiPointLightCtor{ 69583, 70967 };
		constexpr REL::RelocationID kNiPointLightSetAttenuation{ 17224, 17626 };
		constexpr REL::RelocationID kShadowSceneNodeAddLight{ 99692, 106326 };
		constexpr REL::RelocationID kShadowSceneNodeRemoveLight{ 99698, 106332 };

		// ShadowSceneNode::LIGHT_CREATE_PARAMS with powerof3's field names; the
		// pinned header has the same 0x30-byte layout under placeholder names.
		struct LightCreateParams
		{
			bool          dynamic = true;
			bool          shadowLight = false;
			bool          portalStrict = false;
			bool          affectLand = true;
			bool          affectWater = true;
			bool          neverFades = true;
			float         fov = 1.5707964f;
			float         falloff = 1.0f;
			float         nearDistance = 5.0f;
			float         depthBias = 1.0f;
			std::uint32_t sceneGraphIndex = 0;
			void*         restrictedNode = nullptr;
			void*         lensFlareData = nullptr;
		};
		static_assert(sizeof(LightCreateParams) == 0x30);

		// CS Light Limit Fix flag bits kept in the NiLight's ambient.red (NOTES 42).
		constexpr std::uint32_t kLlfInitialised = 1u << 8;
		constexpr std::uint32_t kLlfInverseSquare = 1u << 10;
		// ISL's radius formula constants (CS InverseSquareLighting.cpp).
		constexpr float kIslScaledUnitsSq = 0.8f * 70.0f * 70.0f;
		constexpr float kIslDefaultCutoff = 0.05f;
		constexpr float kIslShadowCutoff = 0.022f;

		RE::NiPointLight* CreatePointLight()
		{
			auto* light = RE::malloc<RE::NiPointLight>();
			if (!light) {
				return nullptr;
			}
			std::memset(static_cast<void*>(light), 0, sizeof(RE::NiPointLight));
			using ctor_t = RE::NiPointLight* (*)(RE::NiPointLight*);
			static REL::Relocation<ctor_t> ctor{ kNiPointLightCtor };
			return ctor(light);
		}

		void SetAttenuation(RE::NiPointLight* a_light, float a_radius)
		{
			using func_t = void (*)(RE::NiPointLight*, float);
			static REL::Relocation<func_t> func{ kNiPointLightSetAttenuation };
			func(a_light, a_radius);
		}

		RE::ShadowSceneNode* MainShadowSceneNode()
		{
			return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
		}

		// ISL: radius = sqrt(3920 (8 fade - cutoff size^2) / (2 cutoff)), with the
		// cutoff at its default unless overridden; fade is intensity / 4.
		float IslRadius(float a_fade, float a_size, float a_cutoff, bool a_shadow)
		{
			const float cutoff = a_cutoff >= 1.0f ? (a_shadow ? kIslShadowCutoff : kIslDefaultCutoff) : std::clamp(a_cutoff, 0.01f, 1.0f);
			const float intensity = a_fade * 4.0f;
			const float radius = std::sqrt(kIslScaledUnitsSq * ((2.0f * intensity - cutoff * a_size * a_size) / (2.0f * cutoff)));
			return std::isfinite(radius) && radius > 1.0f ? radius : 1.0f;
		}

		// The engine exposes no constructor for NiAlphaProperty; the object is
		// laid out by hand: vtable, zero refcount, empty NiObjectNET, flags (NOTES 32).
		RE::NiPointer<RE::NiAlphaProperty> CreateAlphaProperty(bool a_additive, float a_alphaTest)
		{
			auto* property = RE::malloc<RE::NiAlphaProperty>();
			if (!property) {
				return nullptr;
			}
			std::memset(static_cast<void*>(property), 0, sizeof(RE::NiAlphaProperty));
			*reinterpret_cast<std::uintptr_t*>(property) = RE::VTABLE_NiAlphaProperty[0].address();
			property->SetAlphaBlending(true);
			property->SetSrcBlendMode(RE::NiAlphaProperty::AlphaFunction::kSrcAlpha);
			property->SetDestBlendMode(a_additive ? RE::NiAlphaProperty::AlphaFunction::kOne : RE::NiAlphaProperty::AlphaFunction::kInvSrcAlpha);
			property->SetAlphaTesting(a_alphaTest > 0.0f);
			property->alphaThreshold = static_cast<std::uint8_t>(std::clamp(a_alphaTest, 0.0f, 1.0f) * 255.0f);
			return RE::NiPointer<RE::NiAlphaProperty>{ property };
		}

		// A private NiSkinData whose bone array the shell may edit (NOTES 35).
		RE::NiPointer<RE::NiSkinData> CopySkinData(const RE::NiSkinData& a_source)
		{
			auto* copy = RE::malloc<RE::NiSkinData>();
			if (!copy) {
				return nullptr;
			}
			std::memcpy(static_cast<void*>(copy), static_cast<const void*>(&a_source), sizeof(RE::NiSkinData));
			*reinterpret_cast<std::uintptr_t*>(copy) = RE::VTABLE_NiSkinData[0].address();
			reinterpret_cast<volatile std::uint32_t*>(copy)[2] = 0;  // NiRefObject refcount
			std::memset(static_cast<void*>(&copy->skinPartition), 0, sizeof(copy->skinPartition));
			copy->skinPartition = a_source.skinPartition;
			copy->boneData = nullptr;
			if (a_source.bones && a_source.boneData) {
				auto* bones = RE::malloc<RE::NiSkinData::BoneData>(sizeof(RE::NiSkinData::BoneData) * a_source.bones);
				if (!bones) {
					RE::NiPointer<RE::NiSkinData> drop{ copy };
					return nullptr;
				}
				std::memcpy(static_cast<void*>(bones), static_cast<const void*>(a_source.boneData), sizeof(RE::NiSkinData::BoneData) * a_source.bones);
				for (std::uint32_t i = 0; i < a_source.bones; ++i) {
					bones[i].boneVertData = nullptr;
				}
				copy->boneData = bones;
			}
			return RE::NiPointer<RE::NiSkinData>{ copy };
		}

		RE::NiColor ToNi(const Vec3& a_v)
		{
			return RE::NiColor{ a_v.x, a_v.y, a_v.z };
		}
	}

	std::string ShellSuffix()
	{
		return Identity::ShellNodeSuffix();
	}

	// ---------------------------------------------------------- MaterialBinding

	// ---------------------------------------------------------------- slots

	namespace
	{
		// The material field a slot writes; null for the slots phase 3 binds
		// (glint, coat and subsurface are parameter sets, not maps).
		RE::NiPointer<RE::NiSourceTexture>* TextureFieldOf(PBRMaterialLayout& a_material, Slot a_slot)
		{
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
				return &a_material.featuresTexture1;  // fuzz colour in rgb, weight in a
			case Slot::kCoat:
			case Slot::kSubsurface:
				return &a_material.featuresTexture0;  // coat colour + strength, or subsurface colour + thickness
			default:
				return nullptr;  // glint has no map
			}
		}

		std::string TextureName(const RE::NiSourceTexture* a_texture)
		{
			if (!a_texture) {
				return "(none)";
			}
			return a_texture->name.c_str() ? a_texture->name.c_str() : "(unnamed)";
		}

		// CS evaluates fuzz only on materials without a coat or hair model
		// (TruePBR.cpp SetupMaterial).
		bool FuzzPossible(const PBRMaterialLayout& a_material)
		{
			return (a_material.pbrFlags & (kPbrTwoLayer | kPbrHairMarschner)) == 0;
		}
	}

	SlotWriter::SlotWriter(PBRMaterialLayout* a_material, RE::BSLightingShaderProperty* a_property) :
		material_(a_material), property_(a_property)
	{}

	std::string SlotWriter::Problem(Slot a_slot) const
	{
		if (!material_ || !property_) {
			return "no material";
		}
		if (!TextureFieldOf(*material_, a_slot) && a_slot != Slot::kGlint) {
			return std::format("slot '{}' has no place on a PBR material", SlotName(a_slot));
		}
		if (a_slot == Slot::kEmissive && !property_->emissiveColor) {
			return "the property has no emissive colour storage";
		}
		// CS evaluates coat, then hair, then subsurface and either fuzz or
		// glint; coat and subsurface share one map, fuzz and glint exclude each
		// other, and a hair material takes none of them.
		const bool hair = (material_->pbrFlags & kPbrHairMarschner) != 0;
		if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint || a_slot == Slot::kCoat || a_slot == Slot::kSubsurface) && hair) {
			return "the material has a hair model, which CS evaluates instead";
		}
		if ((a_slot == Slot::kFuzz || a_slot == Slot::kGlint) && !FuzzPossible(*material_) && !coat_) {
			return "the material has a coat model, which CS evaluates instead of fuzz and glint";
		}
		if (a_slot == Slot::kFuzz && glint_) {
			return "glint was written on this material; fuzz and glint exclude each other";
		}
		if (a_slot == Slot::kGlint && fuzz_) {
			return "fuzz was written on this material; fuzz and glint exclude each other";
		}
		if (a_slot == Slot::kCoat && (subsurface_ || (material_->pbrFlags & kPbrSubsurface))) {
			return "the material carries subsurface; coat and subsurface share one map";
		}
		if (a_slot == Slot::kSubsurface && (coat_ || (material_->pbrFlags & kPbrTwoLayer))) {
			return "the material carries a coat; coat and subsurface share one map";
		}
		return {};
	}

	void SlotWriter::WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture)
	{
		auto* field = material_ ? TextureFieldOf(*material_, a_slot) : nullptr;
		if (!field) {
			return;
		}
		auto& saved = textures_[static_cast<std::size_t>(a_slot)];
		if (!saved) {
			saved = SavedTexture{ *field, field->get() };
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
		auto* target = a_texture ? a_texture : saved->original.get();
		if (field->get() != target) {
			*field = RE::NiPointer<RE::NiSourceTexture>{ target };
		}
		saved->written = target;
	}

	void SlotWriter::WriteEmissive(const Vec3& a_color, float a_multiplier)
	{
		if (!property_ || !property_->emissiveColor) {
			return;
		}
		if (!emissive_) {
			emissive_ = SavedEmissive{ *property_->emissiveColor, property_->emissiveMult, property_->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit) };
			property_->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
		}
		*property_->emissiveColor = ToNi(a_color);
		property_->emissiveMult = a_multiplier;
	}

	void SlotWriter::SetFeature(std::uint32_t a_bits, bool a_on)
	{
		if (!material_) {
			return;
		}
		if (!flags_) {
			flags_ = material_->pbrFlags;
		}
		if (a_on) {
			material_->pbrFlags |= a_bits;
		} else {
			// Off means back to what the material had for these bits.
			material_->pbrFlags = (material_->pbrFlags & ~a_bits) | (*flags_ & a_bits);
		}
	}

	void SlotWriter::EnableFuzz()
	{
		if (!material_ || fuzz_ || !FuzzPossible(*material_)) {
			return;
		}
		fuzz_ = SavedFuzz{ material_->fuzzColor, material_->fuzzWeight };
		SetFeature(kPbrFuzz, true);
	}

	void SlotWriter::WriteFuzz(const Vec3& a_color, float a_weight)
	{
		EnableFuzz();
		if (!fuzz_) {
			return;
		}
		material_->fuzzColor = ToNi(a_color);
		material_->fuzzWeight = std::clamp(a_weight, 0.0f, 1.0f);
	}

	void SlotWriter::WriteGlint(float a_screenSpaceScale, float a_logMicrofacetDensity, float a_microfacetRoughness, float a_densityRandomization, bool a_enabled)
	{
		if (!material_) {
			return;
		}
		if (!glint_) {
			glint_ = SavedGlint{ material_->glintParameters };
		}
		auto& g = material_->glintParameters;
		g.enabled = a_enabled;
		g.screenSpaceScale = a_screenSpaceScale;
		g.logMicrofacetDensity = a_logMicrofacetDensity;
		g.microfacetRoughness = a_microfacetRoughness;
		g.densityRandomization = a_densityRandomization;
	}

	void SlotWriter::EnableCoat()
	{
		if (!material_ || coat_) {
			return;
		}
		coat_ = SavedCoat{ material_->coatRoughness, material_->coatSpecularLevel };
		SetFeature(kPbrTwoLayer | kPbrColoredCoat, true);
	}

	void SlotWriter::WriteCoat(float a_roughness, float a_level)
	{
		EnableCoat();
		if (!coat_) {
			return;
		}
		material_->coatRoughness = std::clamp(a_roughness, 0.0f, 1.0f);
		material_->coatSpecularLevel = std::clamp(a_level, 0.0f, 1.0f);
	}

	void SlotWriter::EnableSubsurface()
	{
		if (!material_ || subsurface_) {
			return;
		}
		// CS keeps the subsurface colour in specularColor and its opacity in subSurfaceLightRolloff.
		subsurface_ = SavedSubsurface{ material_->specularColor, material_->subSurfaceLightRolloff };
		SetFeature(kPbrSubsurface, true);
	}

	void SlotWriter::WriteSubsurface(const Vec3& a_color, float a_thickness)
	{
		EnableSubsurface();
		if (!subsurface_) {
			return;
		}
		material_->specularColor = ToNi(a_color);
		material_->subSurfaceLightRolloff = std::clamp(a_thickness, 0.0f, 1.0f);
	}

	void SlotWriter::WriteHeightScale(float a_scale)
	{
		if (!material_) {
			return;
		}
		// CS keeps the PBR displacement scale in rimLightPower (NOTES 22).
		if (!heightScale_) {
			heightScale_ = material_->rimLightPower;
		}
		material_->rimLightPower = a_scale;
	}

	bool SlotWriter::StillOwned() const noexcept
	{
		if (!material_ || !property_ || property_->material != material_) {
			return false;
		}
		for (std::size_t i = 0; i < kSlotCount; ++i) {
			const auto& saved = textures_[i];
			if (!saved) {
				continue;
			}
			const auto* field = TextureFieldOf(*material_, static_cast<Slot>(i));
			if (field && field->get() != saved->written) {
				return false;
			}
		}
		return true;
	}

	void SlotWriter::Restore()
	{
		if (!StillOwned()) {
			return;
		}
		for (std::size_t i = 0; i < kSlotCount; ++i) {
			if (const auto& saved = textures_[i]) {
				*TextureFieldOf(*material_, static_cast<Slot>(i)) = saved->original;
			}
		}
		if (emissive_ && property_->emissiveColor) {
			*property_->emissiveColor = emissive_->color;
			property_->emissiveMult = emissive_->multiplier;
			if (!emissive_->ownEmit) {
				property_->flags.reset(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
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

	std::vector<SlotState> SlotWriter::Slots() const
	{
		std::vector<SlotState> out;
		for (std::size_t i = 0; i < kSlotCount; ++i) {
			if (const auto& saved = textures_[i]) {
				out.push_back({ static_cast<Slot>(i), TextureName(saved->original.get()), TextureName(saved->written) });
			}
		}
		return out;
	}

	// ------------------------------------------------------------- material

	std::unique_ptr<MaterialBinding> MaterialBinding::Install(RE::BSGeometry* a_geometry, RE::BSLightingShaderProperty* a_property, bool a_uniqueCopy)
	{
		if (!a_geometry || !a_property || !a_property->material) {
			return nullptr;
		}
		std::unique_ptr<MaterialBinding> binding{ new MaterialBinding{} };
		binding->geometry_ = RE::NiPointer{ a_geometry };
		binding->property_ = RE::NiPointer{ a_property };
		if (a_uniqueCopy) {
			// Materials are pooled by content (NOTES 5): a shared one would glow on
			// every wearer, so the property gets its own copy.
			auto* original = a_property->material;
			binding->original_ = RE::BSTSmartPointer<RE::BSShaderMaterial>{ original };
			a_property->SetMaterial(original, true);
			if (a_property->material == original) {
				auto* copy = original->Create();
				if (copy) {
					copy->CopyMembers(original);
					a_property->SetMaterial(copy, true);
				}
			}
			if (a_property->material == original) {
				logger::warn("could not give '{}' a private material; leaving it shared", a_geometry->name.c_str());
				binding->original_.reset();
			}
		}
		binding->material_ = static_cast<PBRMaterialLayout*>(a_property->material);
		binding->slots_ = SlotWriter{ binding->material_, a_property };
		return binding;
	}

	MaterialBinding::~MaterialBinding()
	{
		auto* property = property_.get();
		if (!property) {
			return;
		}
		if (!StillOwned()) {
			logger::info("restore skipped: '{}' changed hands", geometry_ ? geometry_->name.c_str() : "?");
			return;
		}
		slots_.Restore();
		if (original_) {
			property->SetMaterial(original_.get(), true);
		}
	}

	// ------------------------------------------------------------- ShellBinding

	std::unique_ptr<ShellBinding> ShellBinding::Create(RE::BSGeometry* a_original, RE::BSLightingShaderProperty* a_property, const ShellSettings& a_settings)
	{
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		if (!a_original || !a_property || !a_property->material) {
			return nullptr;
		}
		auto* parent = a_original->parent;
		if (!parent) {
			return nullptr;
		}
		auto* cloned = a_original->Clone();
		auto* clone = cloned ? cloned->AsGeometry() : nullptr;
		if (!clone) {
			logger::warn("shell: Clone() of '{}' returned {}", a_original->name.c_str(), cloned ? "a non-geometry" : "null");
			if (cloned) {
				RE::NiPointer<RE::NiAVObject> drop{ cloned };
			}
			return nullptr;
		}
		std::unique_ptr<ShellBinding> shell{ new ShellBinding{} };
		shell->clone_ = RE::NiPointer<RE::BSGeometry>{ clone };
		clone->name = RE::BSFixedString{ std::string{ a_original->name.c_str() } + ShellSuffix() };

		auto& rt = clone->GetGeometryRuntimeData();
		auto* property = rt.properties[RE::BSGeometry::States::kEffect] ? netimmerse_cast<RE::BSLightingShaderProperty*>(rt.properties[RE::BSGeometry::States::kEffect].get()) : nullptr;
		if (!property || property == a_property) {
			logger::warn("shell: clone of '{}' {} lighting property; shell dropped", a_original->name.c_str(), property ? "shares its" : "has no");
			return nullptr;
		}
		shell->property_ = RE::NiPointer<RE::BSLightingShaderProperty>{ property };
		const auto& originalRt = a_original->GetGeometryRuntimeData();
		const bool  sharedSkin = rt.skinInstance.get() == originalRt.skinInstance.get();
		const bool  sharedBuffers = rt.rendererData == originalRt.rendererData;

		auto* before = property->material;
		if (a_settings.material == ShellMaterial::kPbrCopy) {
			auto* original = a_property->material;
			auto* copy = original->Create();
			if (!copy) {
				logger::warn("shell: could not copy the PBR material of '{}'", a_original->name.c_str());
				return nullptr;
			}
			copy->CopyMembers(original);
			property->SetMaterial(copy, true);
			if (property->material == before || property->material == original) {
				logger::warn("shell: SetMaterial on the clone of '{}' did not install the PBR copy; shell dropped", a_original->name.c_str());
				return nullptr;
			}
			shell->pbr_ = static_cast<PBRMaterialLayout*>(property->material);
			shell->slots_ = SlotWriter{ shell->pbr_, property };
			property->flags.set(Flag::kVertexLighting);  // stays PBR for CS (NOTES 4)
			property->flags.reset(Flag::kRimLighting);
		} else {
			// Vanilla lighting material: white diffuse, the original's normal map,
			// rim lighting from the material's rim power (NOTES 33).
			auto*       vanilla = RE::BSLightingShaderMaterialBase::CreateMaterial(RE::BSShaderMaterial::Feature::kDefault);
			const auto* base = static_cast<const RE::BSLightingShaderMaterialBase*>(a_property->material);
			if (!vanilla) {
				logger::warn("shell: CreateMaterial(kDefault) failed");
				return nullptr;
			}
			const auto* state = RE::BSGraphics::State::GetSingleton();
			auto*       white = state && state->defaultTextureWhite ? netimmerse_cast<RE::NiSourceTexture*>(state->defaultTextureWhite.get()) : nullptr;
			vanilla->diffuseTexture = white ? RE::NiPointer<RE::NiSourceTexture>{ white } : base->diffuseTexture;
			vanilla->normalTexture = base->normalTexture;
			vanilla->textureSet = base->textureSet;
			vanilla->textureClampMode = base->textureClampMode;
			vanilla->materialAlpha = 1.0f;
			vanilla->specularColor = RE::NiColor{ 0.0f, 0.0f, 0.0f };
			vanilla->specularColorScale = 0.0f;
			vanilla->specularPower = 30.0f;
			vanilla->subSurfaceLightRolloff = 0.3f;
			property->SetMaterial(vanilla, true);
			if (property->material == before || property->material == a_property->material) {
				logger::warn("shell: SetMaterial on the clone of '{}' did not install the vanilla material; shell dropped", a_original->name.c_str());
				return nullptr;
			}
			shell->vanilla_ = static_cast<RE::BSLightingShaderMaterialBase*>(property->material);
			property->flags.reset(Flag::kVertexLighting);
			property->flags.set(Flag::kRimLighting);
			property->flags.set(Flag::kOwnEmit);
		}

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
		if (!property->emissiveColor || property->emissiveColor == a_property->emissiveColor) {
			auto* color = RE::malloc<RE::NiColor>();
			if (!color) {
				return nullptr;
			}
			*color = RE::NiColor{ 0.0f, 0.0f, 0.0f };
			property->emissiveColor = color;
			ownEmissive = true;
		}

		shell->alpha_ = CreateAlphaProperty(a_settings.blend == ShellBlend::kAdditive, a_settings.alphaTest);
		if (!shell->alpha_) {
			return nullptr;
		}
		rt.properties[RE::BSGeometry::States::kProperty] = shell->alpha_;

		// Inflation edits the shell's own skin data; the original's stays untouched.
		std::string inflation = "none (not skinned)";
		if (auto* skin = rt.skinInstance.get(); skin && skin->skinData && skin->skinData->boneData && skin->skinData->bones > 0) {
			const auto* originalSkin = originalRt.skinInstance.get();
			if (skin == originalSkin) {
				inflation = "none (skin instance shared)";
			} else {
				if (originalSkin && skin->skinData.get() == originalSkin->skinData.get()) {
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
					shell->restSkinToBone_.assign(shell->skinData_->boneData, shell->skinData_->boneData + shell->skinData_->bones);
					for (auto& bone : shell->restSkinToBone_) {
						bone.boneVertData = nullptr;
					}
				}
			}
		}

		property->SetupGeometry(clone);
		property->FinishSetupGeometry(clone);
		property->DoClearRenderPasses();
		parent->AttachChild(clone, true);
		shell->parent_ = RE::NiPointer<RE::NiNode>{ parent };
		RE::NiUpdateData data{};
		clone->Update(data);
		shell->description_ = std::format("shell ({}, {}, skin {}, buffers {}, emissive storage {}, inflation {})",
			a_settings.material == ShellMaterial::kPbrCopy ? "PBR copy" : "vanilla", a_settings.blend == ShellBlend::kAdditive ? "additive" : "alpha",
			sharedSkin ? "shared" : "cloned", sharedBuffers ? "shared" : "cloned", ownEmissive ? "own" : "cloned", inflation);
		return shell;
	}

	ShellBinding::~ShellBinding()
	{
		Detach();
	}

	void ShellBinding::Detach()
	{
		if (parent_ && clone_) {
			parent_->DetachChild(clone_.get());
		}
		pbr_ = nullptr;
		vanilla_ = nullptr;
		slots_ = SlotWriter{};
		skinData_.reset();
		restSkinToBone_.clear();
		alpha_.reset();
		property_.reset();
		clone_.reset();
		parent_.reset();
	}

	bool ShellBinding::StillOwned() const noexcept
	{
		const auto* property = property_.get();
		if (!property) {
			return false;
		}
		if (pbr_) {
			return slots_.StillOwned();
		}
		return property->material == vanilla_;
	}

	std::string ShellBinding::Problem(Slot a_slot) const
	{
		if (pbr_) {
			return slots_.Problem(a_slot);
		}
		if (a_slot != Slot::kEmissive) {
			return std::format("a vanilla shell material has no '{}' slot", SlotName(a_slot));
		}
		return property_ && property_->emissiveColor ? std::string{} : "the shell has no emissive colour storage";
	}

	void ShellBinding::WriteTexture(Slot a_slot, RE::NiSourceTexture* a_texture)
	{
		if (pbr_) {
			slots_.WriteTexture(a_slot, a_texture);
		}
	}

	void ShellBinding::WriteEmissive(const Vec3& a_color, float a_multiplier)
	{
		if (pbr_) {
			slots_.WriteEmissive(a_color, a_multiplier);
			return;
		}
		auto* property = property_.get();
		if (!property || !property->emissiveColor) {
			return;
		}
		property->flags.set(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
		*property->emissiveColor = ToNi(a_color);
		property->emissiveMult = a_multiplier;
	}

	void ShellBinding::WriteFuzz(const Vec3& a_color, float a_weight)
	{
		if (pbr_) {
			slots_.WriteFuzz(a_color, a_weight);
		}
	}

	void ShellBinding::WriteHeightScale(float a_scale)
	{
		if (pbr_) {
			slots_.WriteHeightScale(a_scale);
		}
	}

	void ShellBinding::WriteGlint(float a_scale, float a_density, float a_roughness, float a_randomization, bool a_enabled)
	{
		if (pbr_) {
			slots_.WriteGlint(a_scale, a_density, a_roughness, a_randomization, a_enabled);
		}
	}

	void ShellBinding::WriteCoat(float a_roughness, float a_level)
	{
		if (pbr_) {
			slots_.WriteCoat(a_roughness, a_level);
		}
	}

	void ShellBinding::WriteSubsurface(const Vec3& a_color, float a_thickness)
	{
		if (pbr_) {
			slots_.WriteSubsurface(a_color, a_thickness);
		}
	}

	std::vector<SlotState> ShellBinding::Slots() const
	{
		return pbr_ ? slots_.Slots() : std::vector<SlotState>{};
	}

	void ShellBinding::Pose(const Vec3& a_inflate, float a_alpha, float a_rimPower, float a_emissive)
	{
		auto* property = property_.get();
		if (!property) {
			return;
		}
		property->SetMaterialAlpha(std::clamp(a_alpha, 0.0f, 1.0f));
		if (vanilla_) {
			vanilla_->rimLightPower = a_rimPower;
			property->emissiveMult = a_emissive;
		}
		if (skinData_ && !restSkinToBone_.empty() && !(a_inflate == lastInflate_)) {
			// Skyrim bones point along X; Y and Z run across the bone (NOTES 35).
			lastInflate_ = a_inflate;
			const float axis[3]{ 1.0f + a_inflate.x, 1.0f + a_inflate.y, 1.0f + a_inflate.z };
			for (std::uint32_t i = 0; i < restSkinToBone_.size() && i < skinData_->bones; ++i) {
				const auto& rest = restSkinToBone_[i].skinToBone;
				auto&       live = skinData_->boneData[i].skinToBone;
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

	void ShellBinding::SetVisible(bool a_visible)
	{
		if (clone_) {
			clone_->SetAppCulled(!a_visible);
		}
	}

	// ---------------------------------------------------------------- lights

	std::vector<LightPlacement> PlaceLights(const Bones& a_bones, std::span<RE::BSGeometry* const> a_geometries, RE::NiAVObject* a_root, const Vec3& a_offset)
	{
		std::vector<LightPlacement> out;
		const RE::NiPoint3          offset{ a_offset.x, a_offset.y, a_offset.z };
		Match(
			a_bones,
			[&](const NamedBones& named) {
				for (const auto& name : named.bones) {
					auto* object = a_root ? a_root->GetObjectByName(RE::BSFixedString{ name }) : nullptr;
					auto* bone = object ? object->AsNode() : nullptr;
					if (bone) {
						out.push_back({ RE::NiPointer<RE::NiNode>{ bone }, name, offset, 1.0f });
					} else {
						logger::warn("light: bone '{}' not found on the wearer", name);
					}
				}
				for (auto& p : out) {
					p.share = 1.0f / static_cast<float>(out.size());
				}
			},
			[&](const SkinnedBones& skinned) {
				// The bones that carry the most skinned vertices across the recipe's
				// geometries, each light at its bone's skinned centre (NOTES 34).
				struct Candidate
				{
					RE::NiNode*   bone = nullptr;
					std::uint32_t verts = 0;
					std::uint32_t bestVerts = 0;
					RE::NiPoint3  center;
				};
				std::vector<Candidate> candidates;
				for (auto* geometry : a_geometries) {
					const auto* skin = geometry ? geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
					if (!skin || !skin->bones || !skin->skinData || !skin->skinData->boneData) {
						continue;
					}
					const auto* data = skin->skinData.get();
					for (std::uint32_t i = 0; i < data->bones; ++i) {
						auto* bone = skin->bones[i] ? skin->bones[i]->AsNode() : nullptr;
						if (!bone) {
							continue;
						}
						const auto& bd = data->boneData[i];
						auto        it = std::ranges::find(candidates, bone, &Candidate::bone);
						if (it == candidates.end()) {
							candidates.push_back({ bone, 0, 0, {} });
							it = std::prev(candidates.end());
						}
						it->verts += bd.verts;
						if (bd.verts > it->bestVerts) {
							it->bestVerts = bd.verts;
							it->center = bd.bound.center;
						}
					}
				}
				std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) { return a.verts > b.verts; });
				if (candidates.empty()) {
					return;
				}
				const float top = static_cast<float>(candidates.front().verts);
				for (const auto& c : candidates) {
					const float share = top > 0 ? static_cast<float>(c.verts) / top : 1.0f;
					if (share < std::max(skinned.minShare, 0.3f) || out.size() >= std::max<std::uint32_t>(1, skinned.max)) {
						break;
					}
					out.push_back({ RE::NiPointer<RE::NiNode>{ c.bone }, c.bone->name.c_str() ? c.bone->name.c_str() : "?", c.center + offset, share });
				}
			});
		return out;
	}

	std::unique_ptr<LightBinding> LightBinding::Create(const std::vector<LightPlacement>& a_placements, bool a_shadow)
	{
		auto* scene = MainShadowSceneNode();
		if (!scene || a_placements.empty()) {
			logger::warn("light: {}", scene ? "no bones to place on" : "no shadow scene node");
			return nullptr;
		}
		std::unique_ptr<LightBinding> out{ new LightBinding{} };
		out->shadow_ = a_shadow;
		for (const auto& placement : a_placements) {
			if (!placement.bone) {
				continue;
			}
			auto* light = CreatePointLight();
			if (!light) {
				logger::warn("light: NiPointLight constructor returned null");
				continue;
			}
			Entry entry;
			entry.light = RE::NiPointer<RE::NiPointLight>{ light };
			entry.bone = placement.bone;
			entry.name = placement.name;
			entry.share = placement.share;

			light->name = RE::BSFixedString{ Identity::LightNodeName() };
			light->local.translate = placement.offset;
			auto& ld = light->GetLightRuntimeData();
			// ISL's overlay: flags in ambient.red, cutoff in ambient.green, source
			// size in radius.z (CS InverseSquareLighting/Common.h).
			ld.ambient = RE::NiColor{ std::bit_cast<float>(kLlfInitialised | kLlfInverseSquare), 1.0f, 0.0f };
			ld.diffuse = RE::NiColor{ 0.0f, 0.0f, 0.0f };
			ld.radius = RE::NiPoint3{ 1.0f, 1.0f, 1.4142f };
			ld.fade = 0.0f;
			SetAttenuation(light, 1.0f);

			placement.bone->AttachChild(light, true);
			RE::NiUpdateData data{};
			light->Update(data);

			LightCreateParams params{};
			params.shadowLight = a_shadow;
			using add_t = RE::BSLight* (*)(RE::ShadowSceneNode*, RE::NiLight*, const LightCreateParams&);
			static REL::Relocation<add_t> add{ kShadowSceneNodeAddLight };
			auto*                         bsLight = add(scene, light, params);
			if (!bsLight) {
				logger::warn("light: ShadowSceneNode::AddLight returned null for {}", placement.name);
				placement.bone->DetachChild(light);
				continue;
			}
			entry.bsLight = RE::NiPointer<RE::BSLight>{ bsLight };
			out->entries_.push_back(std::move(entry));
		}
		return out->entries_.empty() ? nullptr : std::move(out);
	}

	LightBinding::~LightBinding()
	{
		auto* scene = MainShadowSceneNode();
		for (auto& entry : entries_) {
			if (entry.bsLight && scene) {
				using remove_t = void (*)(RE::ShadowSceneNode*, const RE::NiPointer<RE::BSLight>&);
				static REL::Relocation<remove_t> remove{ kShadowSceneNodeRemoveLight };
				remove(scene, entry.bsLight);
			}
			entry.bsLight.reset();
			if (entry.bone && entry.light) {
				entry.bone->DetachChild(entry.light.get());
			}
		}
		entries_.clear();
	}

	void LightBinding::Update(const Vec3& a_color, float a_intensity, float a_size, float a_cutoff, bool a_visible)
	{
		const float size = std::clamp(a_size, 0.01f, 50.0f);
		for (auto& entry : entries_) {
			if (!entry.light) {
				continue;
			}
			auto&       ld = entry.light->GetLightRuntimeData();
			const float fade = a_visible ? std::max(0.0f, a_intensity) * entry.share / 4.0f : 0.0f;
			ld.diffuse = a_visible ? ToNi(a_color) : RE::NiColor{ 0.0f, 0.0f, 0.0f };
			ld.fade = fade;
			ld.ambient.green = a_cutoff;
			ld.radius.z = size;
			// The reach ISL will derive, written for the non-ISL path too.
			const float radius = IslRadius(fade, size, a_cutoff, shadow_);
			if (std::fabs(ld.radius.x - radius) > 1.0f) {
				ld.radius.x = radius;
				ld.radius.y = radius;
				SetAttenuation(entry.light.get(), radius);
			}
		}
	}

	std::string LightBinding::Describe() const
	{
		std::string names;
		for (const auto& entry : entries_) {
			names += std::format("{}{} x{:.2f}", names.empty() ? "" : ", ", entry.name, entry.share);
		}
		return std::format("light on {}{}", names, shadow_ ? " (shadow)" : "");
	}
}
