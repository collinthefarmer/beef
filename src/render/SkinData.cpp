#include "render/SkinData.h"
#include <cstring>

namespace BetterEnchantmentEffects {
namespace {
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
