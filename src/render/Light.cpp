// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Binding.h"

#include "Identity.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace BetterEnchantmentEffects {
namespace {
constexpr REL::RelocationID kNiPointLightCtor{69583, 70967};
constexpr REL::RelocationID kNiPointLightSetAttenuation{17224, 17626};
constexpr REL::RelocationID kShadowSceneNodeAddLight{99692, 106326};
constexpr REL::RelocationID kShadowSceneNodeRemoveLight{99698, 106332};

struct LightCreateParams {
  bool dynamic = true;
  bool shadowLight = false;
  bool portalStrict = false;
  bool affectLand = true;
  bool affectWater = true;
  bool neverFades = true;
  float fov = 1.5707964f;
  float falloff = 1.0f;
  float nearDistance = 5.0f;
  float depthBias = 1.0f;
  std::uint32_t sceneGraphIndex = 0;
  void *restrictedNode = nullptr;
  void *lensFlareData = nullptr;
};
static_assert(sizeof(LightCreateParams) == 0x30);

constexpr std::uint32_t kLlfInitialised = 1u << 8;
constexpr std::uint32_t kLlfInverseSquare = 1u << 10;
constexpr float kIslScaledUnitsSq = 0.8f * 70.0f * 70.0f;
constexpr float kIslDefaultCutoff = 0.05f;
constexpr float kIslShadowCutoff = 0.022f;

RE::NiColor ToNi(const Vec3 &a_v) { return RE::NiColor{a_v.x, a_v.y, a_v.z}; }

RE::NiPointLight *CreatePointLight() {
  auto *light = RE::malloc<RE::NiPointLight>();
  if (!light) {
    return nullptr;
  }
  std::memset(static_cast<void *>(light), 0, sizeof(RE::NiPointLight));
  using ctor_t = RE::NiPointLight *(*)(RE::NiPointLight *);
  static REL::Relocation<ctor_t> ctor{kNiPointLightCtor};
  return ctor(light);
}

void SetAttenuation(RE::NiPointLight *a_light, float a_radius) {
  using func_t = void (*)(RE::NiPointLight *, float);
  static REL::Relocation<func_t> func{kNiPointLightSetAttenuation};
  func(a_light, a_radius);
}

RE::ShadowSceneNode *MainShadowSceneNode() {
  return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
}

float IslRadius(float a_fade, float a_size, float a_cutoff, bool a_shadow) {
  const float cutoff = a_cutoff >= 1.0f
                           ? (a_shadow ? kIslShadowCutoff : kIslDefaultCutoff)
                           : std::clamp(a_cutoff, 0.01f, 1.0f);
  const float intensity = a_fade * 4.0f;
  const float radius = std::sqrt(
      kIslScaledUnitsSq *
      ((2.0f * intensity - cutoff * a_size * a_size) / (2.0f * cutoff)));
  return std::isfinite(radius) && radius > 1.0f ? radius : 1.0f;
}
}

namespace {
std::vector<LightPlacement> PlaceNamedBones(const NamedBones &a_bones,
                                            RE::NiAVObject *a_root,
                                            const RE::NiPoint3 &a_offset) {
  std::vector<LightPlacement> out;
  for (const auto &name : a_bones.bones) {
    auto *object =
        a_root ? a_root->GetObjectByName(RE::BSFixedString{name}) : nullptr;
    auto *bone = object ? object->AsNode() : nullptr;
    if (bone) {
      out.push_back({RE::NiPointer<RE::NiNode>{bone}, name, a_offset, 1.0f});
    } else {
      logger::warn("light: bone '{}' not found on the wearer", name);
    }
  }
  for (auto &placement : out) {
    placement.share = 1.0f / static_cast<float>(out.size());
  }
  return out;
}

struct BoneInfluence {
  RE::NiNode *bone = nullptr;
  std::uint32_t totalVertices = 0;
  std::uint32_t largestVertexCount = 0;
  RE::NiPoint3 center;
};

void AddBoneInfluences(std::vector<BoneInfluence> &a_candidates,
                       const RE::NiSkinInstance &a_skin) {
  const auto *data = a_skin.skinData.get();
  for (std::uint32_t i = 0; i < data->bones; ++i) {
    auto *bone = a_skin.bones[i] ? a_skin.bones[i]->AsNode() : nullptr;
    if (!bone) {
      continue;
    }
    const auto &boneData = data->boneData[i];
    BoneInfluence *it = FindBy(a_candidates, bone, &BoneInfluence::bone);
    if (!it) {
      a_candidates.push_back({bone, 0, 0, {}});
      it = &a_candidates.back();
    }
    it->totalVertices += boneData.verts;
    if (boneData.verts > it->largestVertexCount) {
      it->largestVertexCount = boneData.verts;
      it->center = boneData.bound.center;
    }
  }
}

std::vector<BoneInfluence>
RankBoneInfluences(std::span<RE::BSGeometry *const> a_geometries) {
  std::vector<BoneInfluence> candidates;
  for (auto *geometry : a_geometries) {
    const auto *skin =
        geometry ? geometry->GetGeometryRuntimeData().skinInstance.get()
                 : nullptr;
    if (skin && skin->bones && skin->skinData && skin->skinData->boneData) {
      AddBoneInfluences(candidates, *skin);
    }
  }
  std::ranges::sort(candidates,
                    [](const BoneInfluence &a, const BoneInfluence &b) {
                      return a.totalVertices > b.totalVertices;
                    });
  return candidates;
}

std::vector<LightPlacement>
PlaceSkinnedBones(const SkinnedBones &a_bones,
                  std::span<RE::BSGeometry *const> a_geometries,
                  const RE::NiPoint3 &a_offset) {
  const auto candidates = RankBoneInfluences(a_geometries);
  std::vector<LightPlacement> out;
  if (candidates.empty()) {
    return out;
  }
  const float top = static_cast<float>(candidates.front().totalVertices);
  for (const auto &candidate : candidates) {
    const float share =
        top > 0 ? static_cast<float>(candidate.totalVertices) / top : 1.0f;
    if (share < std::max(a_bones.minShare, 0.3f) ||
        out.size() >= std::max<std::uint32_t>(1, a_bones.max)) {
      break;
    }
    out.push_back(
        {RE::NiPointer<RE::NiNode>{candidate.bone},
         candidate.bone->name.c_str() ? candidate.bone->name.c_str() : "?",
         candidate.center + a_offset, share});
  }
  return out;
}
}

std::vector<LightPlacement>
PlaceLightNodes(const Bones &a_bones,
                std::span<RE::BSGeometry *const> a_geometries,
                RE::NiAVObject *a_root, const Vec3 &a_offset) {
  const RE::NiPoint3 offset{a_offset.x, a_offset.y, a_offset.z};
  return Match(
      a_bones,
      [&](const NamedBones &named) {
        return PlaceNamedBones(named, a_root, offset);
      },
      [&](const SkinnedBones &skinned) {
        return PlaceSkinnedBones(skinned, a_geometries, offset);
      });
}

std::unique_ptr<LightBinding>
LightBinding::Create(const std::vector<LightPlacement> &a_placements,
                     bool a_shadow) {
  auto *scene = MainShadowSceneNode();
  if (!scene || a_placements.empty()) {
    logger::warn("light: {}",
                 scene ? "no bones to place on" : "no shadow scene node");
    return nullptr;
  }
  std::unique_ptr<LightBinding> out{new LightBinding{}};
  static REL::Relocation<RemoveLight> remove{kShadowSceneNodeRemoveLight};
  out->removeLight_ = remove.get();
  out->shadow_ = a_shadow;
  out->scene_ = RE::NiPointer<RE::ShadowSceneNode>{scene};
  out->entries_.reserve(a_placements.size());
  for (const auto &placement : a_placements) {
    if (!placement.bone) {
      continue;
    }
    auto *light = CreatePointLight();
    if (!light) {
      logger::warn("light: NiPointLight constructor returned null");
      continue;
    }
    Entry entry;
    entry.light = RE::NiPointer<RE::NiPointLight>{light};
    entry.bone = placement.bone;
    entry.name = placement.name;
    entry.share = placement.share;

    light->name = RE::BSFixedString{Identity::LightNodeName()};
    light->local.translate = placement.offset;
    auto &ld = light->GetLightRuntimeData();
    ld.ambient = RE::NiColor{
        std::bit_cast<float>(kLlfInitialised | kLlfInverseSquare), 1.0f, 0.0f};
    ld.diffuse = RE::NiColor{0.0f, 0.0f, 0.0f};
    ld.radius = RE::NiPoint3{1.0f, 1.0f, 1.4142f};
    ld.fade = 0.0f;
    SetAttenuation(light, 1.0f);

    placement.bone->AttachChild(light, true);
    RE::NiUpdateData data{};
    light->Update(data);

    LightCreateParams params{};
    params.shadowLight = a_shadow;
    using add_t = RE::BSLight *(*)(RE::ShadowSceneNode *, RE::NiLight *,
                                   const LightCreateParams &);
    static REL::Relocation<add_t> add{kShadowSceneNodeAddLight};
    auto *bsLight = add(scene, light, params);
    if (!bsLight) {
      logger::warn("light: ShadowSceneNode::AddLight returned null for {}",
                   placement.name);
      placement.bone->DetachChild(light);
      continue;
    }
    entry.bsLight = RE::NiPointer<RE::BSLight>{bsLight};
    out->entries_.push_back(std::move(entry));
  }
  return out->entries_.empty() ? nullptr : std::move(out);
}

LightBinding::~LightBinding() {
  auto *scene = scene_.get();
  for (auto &entry : entries_) {
    if (entry.bsLight && scene) {
      removeLight_(scene, entry.bsLight);
    }
    entry.bsLight.reset();
    if (entry.bone && entry.light) {
      entry.bone->DetachChild(entry.light.get());
    }
  }
  entries_.clear();
}

void LightBinding::Update(const LightValues &a_light, bool a_visible) {
  const float size = std::clamp(a_light.size, 0.01f, 50.0f);
  for (auto &entry : entries_) {
    if (!entry.light) {
      continue;
    }
    auto &ld = entry.light->GetLightRuntimeData();
    const float fade =
        a_visible ? std::max(0.0f, a_light.intensity) * entry.share / 4.0f
                  : 0.0f;
    ld.diffuse =
        a_visible ? ToNi(a_light.color) : RE::NiColor{0.0f, 0.0f, 0.0f};
    ld.fade = fade;
    ld.ambient.green = a_light.cutoff;
    ld.radius.z = size;
    const float radius = IslRadius(fade, size, a_light.cutoff, shadow_);
    if (std::fabs(ld.radius.x - radius) > 1.0f) {
      ld.radius.x = radius;
      ld.radius.y = radius;
      SetAttenuation(entry.light.get(), radius);
    }
  }
}

std::string LightBinding::Describe() const {
  std::string names;
  for (const auto &entry : entries_) {
    names += std::format("{}{} x{:.2f}", names.empty() ? "" : ", ", entry.name,
                         entry.share);
  }
  return std::format("light on {}{}", names, shadow_ ? " (shadow)" : "");
}
}
