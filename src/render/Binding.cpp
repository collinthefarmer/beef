#include "render/Binding.h"
#include "diagnostics/Trace.h"

#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

namespace {
std::uint32_t FeatureMask(Slot a_slot) {
  switch (a_slot) {
  case Slot::kFuzz:
    return kPbrFuzz;
  case Slot::kCoat:
    return kPbrTwoLayer | kPbrColoredCoat;
  case Slot::kSubsurface:
    return kPbrSubsurface;
  default:
    return 0;
  }
}
}

std::list<SlotWriter::PublishedTexture> &SlotWriter::RetiredTextures() {
  // Process-lived because engine materials can outlive plugin singletons.
  // Initialized while preparing a write, never for the first time in
  // retirement.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  static auto *textures = new std::list<PublishedTexture>;
  return *textures;
}

void SweepRetiredMaterialTextures() {
  auto &retired = SlotWriter::RetiredTextures();
  if (retired.empty())
    return;
  std::unordered_map<PBRMaterialLayout *, std::uint32_t> retainedOwners;
  for (const auto &entry : retired)
    ++retainedOwners[entry.material.get()];
  std::unordered_set<PBRMaterialLayout *> unreferenced;
  std::unordered_set<PBRMaterialLayout *> inspected;
  for (const auto &entry : retired) {
    auto *material = entry.material.get();
    if (!inspected.insert(material).second)
      continue;
    const auto ours = retainedOwners.at(material);
    // Sample through the engine's atomic intrusive-refcount API. If only our
    // journal records retain this material, no engine consumer still owns it.
    const auto count = material->IncRef();
    material->DecRef();
    if (count == ours + 1)
      unreferenced.insert(material);
  }
  std::erase_if(retired, [&](const SlotWriter::PublishedTexture &a_entry) {
    const auto *field = TextureFieldOf(*a_entry.material, a_entry.slot);
    return unreferenced.contains(a_entry.material.get()) || !field ||
           field->get() != a_entry.texture.get();
  });
}

SlotWriter::SlotWriter(PbrMaterial a_material)
    : binding_(std::move(a_material)), traceID_(Trace::NextID()) {}

bool SlotWriter::MaterialAttached() const noexcept {
  return binding_.Attached();
}

bool SlotWriter::HasGroup(Slot a_slot) const {
  return groups_[static_cast<std::size_t>(a_slot)].has_value();
}

SlotWriter::GroupState SlotWriter::Capture(Slot a_slot) const {
  GroupState state;
  const auto &material = *binding_.material_;
  if (const auto *field = TextureFieldOf(*binding_.material_, a_slot)) {
    state.texture = reinterpret_cast<std::uintptr_t>(field->get());
  }
  state.flags = material.pbrFlags & FeatureMask(a_slot);
  switch (a_slot) {
  case Slot::kEmissive: {
    const auto &property = *binding_.property_;
    state.storage = reinterpret_cast<std::uintptr_t>(property.emissiveColor);
    if (property.emissiveColor) {
      const auto &color = *property.emissiveColor;
      state.color = {color.red, color.green, color.blue};
      state.scalar = property.emissiveMult;
    }
    state.flags =
        property.flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
    break;
  }
  case Slot::kFuzz:
    state.color = {material.fuzzColor.red, material.fuzzColor.green,
                   material.fuzzColor.blue};
    state.scalar = material.fuzzWeight;
    break;
  case Slot::kHeight:
    state.scalar = material.rimLightPower;
    break;
  case Slot::kGlint: {
    state.glint = material.glintParameters;
    break;
  }
  case Slot::kCoat:
    state.roughness = material.coatRoughness;
    state.scalar = material.coatSpecularLevel;
    break;
  case Slot::kSubsurface:
    state.color = {material.specularColor.red, material.specularColor.green,
                   material.specularColor.blue};
    state.scalar = material.subSurfaceLightRolloff;
    break;
  default:
    break;
  }
  return state;
}

SlotWriter::Group *SlotWriter::BeginWrite(Slot a_slot, bool a_enableFeature) {
  if (!MaterialAttached() || Problem(a_slot))
    return nullptr;
  auto &group = groups_[static_cast<std::size_t>(a_slot)];
  const auto current = Capture(a_slot);
  if (group)
    return group->state.Owns(current) ? &*group : nullptr;
  const auto *field = TextureFieldOf(*binding_.material_, a_slot);
  TextureRef original{field ? field->get() : nullptr};
  if (!original.Valid())
    return nullptr;
  (void)RetiredTextures();
  published_.push_back({binding_.material_, a_slot, original});
  auto &created = group.emplace(Group{OwnedState{current}, original, original});
  if (a_enableFeature)
    SetFeature(a_slot, true);
  return &created;
}

void SlotWriter::EndWrite(Slot a_slot) {
  auto &group = groups_[static_cast<std::size_t>(a_slot)];
  if (group)
    group->state.Written(Capture(a_slot));
}

void SlotWriter::SetFeature(Slot a_slot, bool a_on) {
  if (!MaterialAttached()) {
    return;
  }
  auto &flags = binding_.material_->pbrFlags;
  const auto mask = FeatureMask(a_slot);
  const auto &group = groups_[static_cast<std::size_t>(a_slot)];
  if (group) {
    flags = (flags & ~mask) |
            (a_on ? mask : (group->state.Original().flags & mask));
  }
}

std::optional<Diagnostic> SlotWriter::Problem(Slot a_slot) const {
  const std::optional<std::string> message = ProblemMessage(a_slot);
  if (!message) {
    return std::nullopt;
  }
  return MakeDiagnostic(Severity::kError, std::string{SlotName(a_slot)},
                        *message);
}

std::optional<std::string> SlotWriter::ProblemMessage(Slot a_slot) const {
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
      !FuzzPossible(*binding_.material_) && !HasGroup(Slot::kCoat)) {
    return "the material has a coat model, which CS evaluates instead of fuzz "
           "and glint";
  }
  if (a_slot == Slot::kFuzz && HasGroup(Slot::kGlint)) {
    return "glint was written on this material; fuzz and glint exclude each "
           "other";
  }
  if (a_slot == Slot::kGlint && HasGroup(Slot::kFuzz)) {
    return "fuzz was written on this material; fuzz and glint exclude each "
           "other";
  }
  if (a_slot == Slot::kCoat &&
      (HasGroup(Slot::kSubsurface) ||
       (binding_.material_->pbrFlags & kPbrSubsurface))) {
    return "the material carries subsurface; coat and subsurface share one map";
  }
  if (a_slot == Slot::kSubsurface &&
      (HasGroup(Slot::kCoat) ||
       (binding_.material_->pbrFlags & kPbrTwoLayer))) {
    return "the material carries a coat; coat and subsurface share one map";
  }
  return std::nullopt;
}

void SlotWriter::WriteTexture(Slot a_slot, const TextureRef &a_texture) {
  if (!a_texture.Valid() || !TextureFieldOf(*binding_.material_, a_slot))
    return;
  auto *group = BeginWrite(a_slot);
  if (!group)
    return;
  const TextureRef &target = a_texture ? a_texture : group->original;
  *TextureFieldOf(*binding_.material_, a_slot) =
      RE::NiPointer<RE::NiSourceTexture>{target.get()};
  group->written = target;
  SetFeature(a_slot, static_cast<bool>(a_texture));
  EndWrite(a_slot);
}

void SlotWriter::WriteEmissive(const Vec3 &a_color, float a_multiplier) {
  if (!BeginWrite(Slot::kEmissive))
    return;
  *binding_.property_->emissiveColor = ToNi(a_color);
  binding_.property_->emissiveMult = a_multiplier;
  binding_.property_->flags.set(
      RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
  EndWrite(Slot::kEmissive);
}
void SlotWriter::WriteFuzz(const Vec3 &a_color, float a_weight) {
  if (!BeginWrite(Slot::kFuzz, true))
    return;
  binding_.material_->fuzzColor = ToNi(a_color);
  binding_.material_->fuzzWeight = std::clamp(a_weight, 0.0f, 1.0f);
  EndWrite(Slot::kFuzz);
}
void SlotWriter::WriteHeightScale(float a_scale) {
  if (!BeginWrite(Slot::kHeight))
    return;
  binding_.material_->rimLightPower = a_scale;
  EndWrite(Slot::kHeight);
}
void SlotWriter::WriteGlint(const GlintParameters &a_parameters) {
  if (!BeginWrite(Slot::kGlint))
    return;
  binding_.material_->glintParameters = a_parameters;
  EndWrite(Slot::kGlint);
}
void SlotWriter::WriteCoat(float a_roughness, float a_level) {
  if (!BeginWrite(Slot::kCoat, true))
    return;
  binding_.material_->coatRoughness = std::clamp(a_roughness, 0.0f, 1.0f);
  binding_.material_->coatSpecularLevel = std::clamp(a_level, 0.0f, 1.0f);
  EndWrite(Slot::kCoat);
}
void SlotWriter::WriteSubsurface(const Vec3 &a_color, float a_thickness) {
  if (!BeginWrite(Slot::kSubsurface, true))
    return;
  binding_.material_->specularColor = ToNi(a_color);
  binding_.material_->subSurfaceLightRolloff =
      std::clamp(a_thickness, 0.0f, 1.0f);
  EndWrite(Slot::kSubsurface);
}

bool SlotWriter::StillOwned() const noexcept {
  if (!MaterialAttached())
    return false;
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    const auto &group = groups_[i];
    if (group && !group->state.Owns(Capture(static_cast<Slot>(i))))
      return false;
  }
  return true;
}

void SlotWriter::RestoreGroup(Slot a_slot, const GroupState &a_state,
                              const TextureRef &a_originalTexture) {
  auto &material = *binding_.material_;
  switch (a_slot) {
  case Slot::kEmissive:
    if (binding_.property_->emissiveColor) {
      *binding_.property_->emissiveColor = ToNi(a_state.color);
    }
    binding_.property_->emissiveMult = a_state.scalar;
    if (a_state.flags)
      binding_.property_->flags.set(
          RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
    else
      binding_.property_->flags.reset(
          RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit);
    break;
  case Slot::kFuzz:
    material.fuzzColor = ToNi(a_state.color);
    material.fuzzWeight = a_state.scalar;
    break;
  case Slot::kHeight:
    material.rimLightPower = a_state.scalar;
    break;
  case Slot::kGlint:
    material.glintParameters = a_state.glint;
    break;
  case Slot::kCoat:
    material.coatRoughness = a_state.roughness;
    material.coatSpecularLevel = a_state.scalar;
    break;
  case Slot::kSubsurface:
    material.specularColor = ToNi(a_state.color);
    material.subSurfaceLightRolloff = a_state.scalar;
    break;
  default:
    break;
  }
  const auto mask = FeatureMask(a_slot);
  material.pbrFlags = (material.pbrFlags & ~mask) | (a_state.flags & mask);
  if (auto *field = TextureFieldOf(material, a_slot)) {
    *field = RE::NiPointer<RE::NiSourceTexture>{a_originalTexture.get()};
  }
}

void SlotWriter::Restore() {
  // Never restore property state through a replaced material attachment.
  if (MaterialAttached()) {
    for (std::size_t i = 0; i < groups_.size(); ++i) {
      const auto &group = groups_[i];
      if (!group)
        continue;
      const auto slot = static_cast<Slot>(i);
      const auto current = Capture(slot);
      const auto original = group->state.Restore(current);
      Trace::Safely([&] {
        Trace::Emit(
            Trace::Event::kRestore,
            {{"binding", std::to_string(traceID_)},
             {"slot", std::string{SlotName(slot)}},
             {"material", Trace::Pointer(binding_.material_.get())},
             {"original_texture",
              std::to_string(group->state.Original().texture)},
             {"written_texture",
              std::to_string(group->state.LastWritten().texture)},
             {"current_texture", std::to_string(current.texture)},
             {"original_flags", std::to_string(group->state.Original().flags)},
             {"written_flags",
              std::to_string(group->state.LastWritten().flags)},
             {"current_flags", std::to_string(current.flags)},
             {"decision",
              original ? "restore_group" : "preserve_external_group"}});
      });
      if (original)
        RestoreGroup(slot, *original, group->original);
    }
  }
  RetainPublishedTextures();
  groups_ = {};
}

void SlotWriter::RetainPublishedTextures() noexcept {
  if (published_.empty())
    return;
  for (auto it = published_.begin(); it != published_.end();) {
    const auto &group = groups_[static_cast<std::size_t>(it->slot)];
    const auto *field = TextureFieldOf(*binding_.material_, it->slot);
    const TextureRef *retained = nullptr;
    if (group && field) {
      if (field->get() == group->written.get())
        retained = &group->written;
      else if (field->get() == group->original.get())
        retained = &group->original;
    }
    if (retained && retained->Generation() != 0) {
      it->texture = *retained;
      ++it;
    } else {
      it = published_.erase(it);
    }
  }
  // Nodes were allocated before the first write. Retirement transfers ownership
  // without allocating, so a destructor cannot lose a lease on allocation
  // failure.
  RetiredTextures().splice(RetiredTextures().end(), published_);
}

std::vector<SlotState> SlotWriter::Slots() const {
  std::vector<SlotState> out;
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    const auto &group = groups_[i];
    if (group) {
      out.push_back({static_cast<Slot>(i), TextureName(group->original.get()),
                     TextureName(group->written.get())});
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
  const RE::BSTSmartPointer<RE::BSShaderMaterial> original{
      a_property->material};
  if (a_uniqueCopy) {
    a_property->SetMaterial(original.get(), true);
    if (a_property->material == original.get()) {
      const RE::BSTSmartPointer<RE::BSShaderMaterial> copy{original->Create()};
      if (copy) {
        copy->CopyMembers(original.get());
        a_property->SetMaterial(copy.get(), true);
      }
    }
    if (a_property->material == original.get()) {
      logger::warn("could not give '{}' a private material; binding dropped",
                   a_geometry->name.c_str());
      return nullptr;
    }
  }
  const std::optional<PbrMaterial> material = PbrMaterial::Bind(a_property);
  if (!material) {
    if (a_uniqueCopy) {
      a_property->SetMaterial(original.get(), true);
    }
    return nullptr;
  }
  binding->slots_.emplace(*material);
  binding->privateMaterial_ = a_uniqueCopy;
  Trace::Safely([&] {
    Trace::Emit(
        Trace::Event::kBinding,
        {{"action", "install"},
         {"geometry", Trace::Pointer(a_geometry)},
         {"name", a_geometry->name.c_str() ? a_geometry->name.c_str() : ""},
         {"property", Trace::Pointer(a_property)},
         {"original_material", Trace::Pointer(original.get())},
         {"installed_material", Trace::Pointer(a_property->material)},
         {"emissive_storage", Trace::Pointer(a_property->emissiveColor)}});
  });
  return binding;
}

MaterialBinding::~MaterialBinding() {
  if (slots_)
    slots_->Restore();
  // Keep the private material attached. Replacing the entire material here
  // would discard external writes to fields that our journal never touched.
}

RE::BSLightingShaderProperty *MaterialBinding::Property() const noexcept {
  return property_.get();
}

bool MaterialBinding::Private() const noexcept { return privateMaterial_; }

std::optional<Diagnostic> MaterialBinding::Problem(Slot a_slot) const {
  if (slots_) {
    return slots_->Problem(a_slot);
  }
  return MakeDiagnostic(Severity::kError, std::string{SlotName(a_slot)},
                        "material unavailable");
}

void MaterialBinding::WriteTexture(Slot a_slot, const TextureRef &a_texture) {
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

void MaterialBinding::WriteGlint(const GlintParameters &a_parameters) {
  if (!slots_ || !StillOwned()) {
    return;
  }
  slots_->WriteGlint(a_parameters);
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
