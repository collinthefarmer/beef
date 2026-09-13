#include "render/SkinPalette.h"

#include "diagnostics/Trace.h"
#include "planners/TransformStorage.h"

#include <optional>
#include <unordered_set>
#include <vector>

namespace BetterEnchantmentEffects {
struct PaletteOwner {
  RE::NiPointer<RE::NiNode> node;
  const void *entries = nullptr;
  std::uint32_t count = 0;
};

struct PaletteLink {
  std::uint32_t index = 0;
  const RE::NiTransform *transform = nullptr;
};

struct SkinPaletteState {
  RE::NiPointer<RE::NiAVObject> root;
  std::vector<PaletteOwner> owners;
  RE::NiPointer<RE::BSGeometry> source;
  RE::NiPointer<RE::NiSkinInstance> sourceSkin;
  RE::NiPointer<RE::NiSkinInstance> cloneSkin;
  const RE::NiTransform **sourceArray = nullptr;
  const RE::NiTransform **cloneArray = nullptr;
  std::vector<PaletteLink> links;
  std::uint32_t count = 0;
};

namespace {
constexpr std::uint32_t kMaxBones = 4096;
static_assert(sizeof(RE::NiTransform) == 0x34);

struct FlattenedStorage {
  const void *entries;
  std::uint32_t count;
};
FlattenedStorage StorageOf(RE::NiNode *a_node) {
  return {REL::RelocateMember<void *>(a_node, 0x130, 0x158),
          REL::RelocateMember<std::uint32_t>(a_node, 0x128, 0x150)};
}
constexpr std::size_t kFlattenedEntryStride = 0x80;
constexpr std::size_t kWorldTransformOffset = 0x34;

std::optional<PaletteOwner> FlattenedOwner(RE::NiNode *a_node) {
  if (!a_node) {
    return std::nullopt;
  }
  const auto *rtti = a_node->GetRTTI();
  const REL::Relocation<const RE::NiRTTI *> flattened{
      RE::NiRTTI_BSFlattenedBoneTree};
  if (!rtti || !rtti->IsKindOf(flattened.get())) {
    return std::nullopt;
  }
  const auto [entries, count] = StorageOf(a_node);
  if (!entries || !count || count > kMaxBones) {
    return std::nullopt;
  }
  return PaletteOwner{RE::NiPointer<RE::NiNode>{a_node}, entries, count};
}

std::vector<PaletteOwner> FindOwners(RE::NiAVObject *a_root) {
  std::vector<PaletteOwner> owners;
  std::vector<RE::NiAVObject *> pending;
  std::unordered_set<RE::NiAVObject *> seen;
  if (a_root) {
    pending.push_back(a_root);
    seen.insert(a_root);
  }
  while (!pending.empty()) {
    auto *object = pending.back();
    pending.pop_back();
    auto *node = object->AsNode();
    if (!node) {
      continue;
    }
    if (auto owner = FlattenedOwner(node)) {
      owners.push_back(std::move(*owner));
    }
    for (const auto &child : node->GetChildren()) {
      if (!child || seen.contains(child.get())) {
        continue;
      }
      if (seen.size() >= kMaxBones) {
        return {};
      }
      seen.insert(child.get());
      pending.push_back(child.get());
    }
  }
  return owners;
}

bool HasOwner(const std::vector<PaletteOwner> &a_owners,
              const RE::NiTransform *a_transform) {
  for (const auto &owner : a_owners) {
    if (TransformStorageIndex(reinterpret_cast<std::uintptr_t>(a_transform),
                              {reinterpret_cast<std::uintptr_t>(owner.entries),
                               owner.count, kFlattenedEntryStride,
                               kWorldTransformOffset})) {
      return true;
    }
  }
  return false;
}

void TracePreservedPalette(const SkinPaletteState &a_lease,
                           RE::BSGeometry &a_clone) {
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kShell,
                {{"action", "palette_preserved"},
                 {"source", Trace::Pointer(a_lease.source.get())},
                 {"clone", Trace::Pointer(&a_clone)},
                 {"root", Trace::Pointer(a_lease.root.get())},
                 {"restored_entries", std::to_string(a_lease.links.size())}});
    for (const auto &owner : a_lease.owners) {
      Trace::Emit(Trace::Event::kShell,
                  {{"action", "palette_owner"},
                   {"clone", Trace::Pointer(&a_clone)},
                   {"owner", Trace::Pointer(owner.node.get())},
                   {"entries", Trace::Pointer(owner.entries)},
                   {"count", std::to_string(owner.count)}});
    }
  });
}

std::unique_ptr<SkinPaletteLease> RejectPalette(std::string_view a_reason) {
  logger::warn("shell: palette rejected: {}", a_reason);
  Trace::Safely([&] {
    Trace::Emit(Trace::Event::kShell, {{"action", "palette_rejected"},
                                       {"reason", std::string{a_reason}}});
  });
  return nullptr;
}
}

std::unique_ptr<SkinPaletteLease>
SkinPaletteLease::Preserve(RE::BSGeometry &a_source, RE::BSGeometry &a_clone) {
  auto *source = a_source.GetGeometryRuntimeData().skinInstance.get();
  auto *clone = a_clone.GetGeometryRuntimeData().skinInstance.get();
  if (!source && !clone) {
    return std::unique_ptr<SkinPaletteLease>{new SkinPaletteLease};
  }
  if (!source || !clone || source == clone || !source->skinData ||
      !clone->skinData || !source->rootParent ||
      source->rootParent != clone->rootParent ||
      source->skinData->bones != clone->skinData->bones || !source->bones ||
      !clone->bones || !source->boneWorldTransforms ||
      !clone->boneWorldTransforms ||
      source->boneWorldTransforms == clone->boneWorldTransforms ||
      source->skinData->bones > kMaxBones) {
    return RejectPalette("incompatible_skin");
  }
  auto owner = std::unique_ptr<SkinPaletteLease>{new SkinPaletteLease};
  auto &lease = *owner->state_;
  lease.root = RE::NiPointer<RE::NiAVObject>{source->rootParent};
  lease.source = RE::NiPointer<RE::BSGeometry>{&a_source};
  lease.sourceSkin = RE::NiPointer<RE::NiSkinInstance>{source};
  lease.cloneSkin = RE::NiPointer<RE::NiSkinInstance>{clone};
  lease.count = source->skinData->bones;
  lease.sourceArray = source->boneWorldTransforms;
  lease.cloneArray = clone->boneWorldTransforms;
  for (std::uint32_t i = 0; i < lease.count; ++i) {
    if (source->bones[i] != clone->bones[i]) {
      return RejectPalette("bone_node_mismatch");
    }
    const auto *transform = source->boneWorldTransforms[i];
    if (transform == clone->boneWorldTransforms[i]) {
      continue;
    }
    if (!transform || clone->boneWorldTransforms[i] || source->bones[i]) {
      return RejectPalette("unexpected_transform_mismatch");
    }
    lease.links.push_back({i, transform});
  }
  if (!lease.links.empty()) {
    lease.owners = FindOwners(lease.root.get());
    for (const auto &link : lease.links) {
      if (!HasOwner(lease.owners, link.transform)) {
        return RejectPalette("transform_owner_unresolved");
      }
    }
  }
  for (const auto &link : lease.links) {
    clone->boneWorldTransforms[link.index] = link.transform;
  }
  TracePreservedPalette(lease, a_clone);
  return owner;
}

SkinPaletteLease::SkinPaletteLease()
    : state_(std::make_unique<SkinPaletteState>()) {}

bool SkinPaletteLease::StillOwned(
    const RE::BSGeometry &a_clone) const noexcept {
  const auto &a_lease = *state_;
  if (a_clone.GetGeometryRuntimeData().skinInstance.get() !=
      a_lease.cloneSkin.get())
    return false;
  if (!a_lease.sourceSkin && !a_lease.cloneSkin) {
    return true;
  }
  const auto *source = a_lease.sourceSkin.get();
  const auto *clone = a_lease.cloneSkin.get();
  if (!source || !clone || !a_lease.source ||
      a_lease.source->GetGeometryRuntimeData().skinInstance.get() != source ||
      !source->skinData || !clone->skinData ||
      source->skinData->bones != a_lease.count ||
      clone->skinData->bones != a_lease.count ||
      source->rootParent != a_lease.root.get() ||
      clone->rootParent != a_lease.root.get() ||
      source->boneWorldTransforms != a_lease.sourceArray ||
      clone->boneWorldTransforms != a_lease.cloneArray) {
    return false;
  }
  for (const auto &owner : a_lease.owners) {
    const auto [entries, count] = StorageOf(owner.node.get());
    if (entries != owner.entries || count != owner.count) {
      return false;
    }
  }
  for (const auto &link : a_lease.links) {
    if (source->boneWorldTransforms[link.index] != link.transform ||
        clone->boneWorldTransforms[link.index] != link.transform) {
      return false;
    }
  }
  return true;
}

SkinPaletteLease::~SkinPaletteLease() {
  auto &a_lease = *state_;
  auto *clone = a_lease.cloneSkin.get();
  if (clone && clone->skinData &&
      clone->boneWorldTransforms == a_lease.cloneArray) {
    for (const auto &link : a_lease.links) {
      if (link.index < clone->skinData->bones &&
          clone->boneWorldTransforms[link.index] == link.transform) {
        clone->boneWorldTransforms[link.index] = nullptr;
      }
    }
  }
  a_lease.links.clear();
}
}
