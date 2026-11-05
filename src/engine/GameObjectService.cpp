// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/GameObjectService.h"
#include "recipe/BusEvents.h"

#include "Core.h"
#include "engine/EngineForms.h"
#include "engine/Tweaks.h"

#include <array>
#include <format>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
using Studio::GameObjectCandidate;
using Studio::GameObjectCatalog;
using Studio::GameObjectKind;
using Studio::kGameObjectKindCount;

std::mutex g_mutex;
std::array<std::shared_ptr<const GameObjectCatalog>, kGameObjectKindCount>
    g_catalogs;
std::unordered_map<std::string, FormKey> g_editorIds;

std::mutex g_animMutex;
std::unordered_map<RE::FormID, std::set<std::string, std::less<>>> g_animTags;
std::unordered_map<RE::FormID, std::shared_ptr<const GameObjectCatalog>>
    g_animCatalogs;
std::unordered_set<RE::FormID> g_animDirty;

std::shared_ptr<const GameObjectCatalog> EmptyCatalog(GameObjectKind a_kind) {
  auto catalog = std::make_shared<GameObjectCatalog>();
  catalog->kind = a_kind;
  return catalog;
}

std::string FullNameOr(RE::TESForm &a_form, const FormKey &a_key) {
  if (const auto *named = a_form.As<RE::TESFullName>()) {
    if (const char *full = named->GetFullName(); full && *full) {
      return full;
    }
  }
  return a_key.ToString();
}

void IndexEditorId(const std::string &a_editorId, const FormKey &a_key) {
  if (!a_editorId.empty()) {
    g_editorIds.emplace(Lower(a_editorId), a_key);
  }
}

template <class Form>
std::shared_ptr<const GameObjectCatalog>
BuildFormCatalog(GameObjectKind a_kind, RE::TESDataHandler &a_handler) {
  auto catalog = std::make_shared<GameObjectCatalog>();
  catalog->kind = a_kind;
  std::set<std::string> plugins;
  for (Form *form : a_handler.GetFormArray<Form>()) {
    if (!form) {
      continue;
    }
    const std::string editorId = EditorIdOf(*form);
    const FormKey key = FormKeyFor(*form);
    IndexEditorId(editorId, key);
    GameObjectCandidate candidate;
    candidate.value = editorId.empty() ? key.ToString() : editorId;
    candidate.display = editorId.empty() ? FullNameOr(*form, key) : editorId;
    candidate.qualifier = key.file;
    catalog->candidates.push_back(std::move(candidate));
    plugins.insert(key.file);
  }
  catalog->sourceNote =
      std::format("{} {} from {} plugin(s)", catalog->candidates.size(),
                  Studio::GameObjectKindName(a_kind), plugins.size());
  return catalog;
}

template <class Form> void IndexFormEditorIds(RE::TESDataHandler &a_handler) {
  for (Form *form : a_handler.GetFormArray<Form>()) {
    if (!form) {
      continue;
    }
    IndexEditorId(EditorIdOf(*form), FormKeyFor(*form));
  }
}

std::shared_ptr<const GameObjectCatalog> BuildActorValueCatalog() {
  auto catalog = std::make_shared<GameObjectCatalog>();
  catalog->kind = GameObjectKind::kActorValue;
  const RE::ActorValueList *list = RE::ActorValueList::GetSingleton();
  if (!list) {
    catalog->sourceNote = "actor value list unavailable";
    return catalog;
  }
  std::uint32_t i = 0;
  for (const RE::ActorValueInfo *info : list->actorValues) {
    const auto value = static_cast<RE::ActorValue>(i++);
    if (!info || !info->enumName || info->enumName[0] == '\0' ||
        list->LookupActorValueByName(info->enumName) != value) {
      continue;
    }
    GameObjectCandidate candidate;
    candidate.value = info->enumName;
    const char *full = info->GetFullName();
    candidate.display = (full && *full) ? full : info->enumName;
    catalog->candidates.push_back(std::move(candidate));
  }
  catalog->sourceNote =
      std::format("{} actor value(s)", catalog->candidates.size());
  return catalog;
}

void RebuildLocked() {
  g_editorIds.clear();
  for (auto &catalog : g_catalogs) {
    catalog.reset();
  }
  RE::TESDataHandler *handler = RE::TESDataHandler::GetSingleton();
  if (!handler) {
    return;
  }
  g_catalogs[IndexOf(GameObjectKind::kKeyword)] =
      BuildFormCatalog<RE::BGSKeyword>(GameObjectKind::kKeyword, *handler);
  g_catalogs[IndexOf(GameObjectKind::kEnchantment)] =
      BuildFormCatalog<RE::EnchantmentItem>(GameObjectKind::kEnchantment,
                                            *handler);
  g_catalogs[IndexOf(GameObjectKind::kEffectShader)] =
      BuildFormCatalog<RE::TESEffectShader>(GameObjectKind::kEffectShader,
                                            *handler);
  g_catalogs[IndexOf(GameObjectKind::kMagicEffect)] =
      BuildFormCatalog<RE::EffectSetting>(GameObjectKind::kMagicEffect,
                                          *handler);
  g_catalogs[IndexOf(GameObjectKind::kArmor)] =
      BuildFormCatalog<RE::TESObjectARMO>(GameObjectKind::kArmor, *handler);
  g_catalogs[IndexOf(GameObjectKind::kLight)] =
      BuildFormCatalog<RE::TESObjectLIGH>(GameObjectKind::kLight, *handler);
  IndexFormEditorIds<RE::TESObjectARMA>(*handler);
  g_catalogs[IndexOf(GameObjectKind::kActorValue)] = BuildActorValueCatalog();
  logger::info("game objects: {} editor IDs indexed{}", g_editorIds.size(),
               TweaksEditorIdsAvailable()
                   ? " (po3's Tweaks answers the rest)"
                   : " (po3's Tweaks not loaded: only the engine's own)");
}
}

void RebuildGameObjectCatalogs() {
  std::scoped_lock lock{g_mutex};
  RebuildLocked();
}

std::shared_ptr<const GameObjectCatalog>
GameObjectCatalogOf(GameObjectKind a_kind) {
  if (Studio::IsViewedActorKind(a_kind)) {
    return EmptyCatalog(a_kind);
  }
  std::scoped_lock lock{g_mutex};
  const std::size_t index = IndexOf(a_kind);
  if (!g_catalogs[index]) {
    if (a_kind == GameObjectKind::kActorValue) {
      g_catalogs[index] = BuildActorValueCatalog();
    } else {
      RebuildLocked();
    }
  }
  return g_catalogs[index] ? g_catalogs[index] : EmptyCatalog(a_kind);
}

std::shared_ptr<const GameObjectCatalog>
AnimEventCatalogOf(RE::FormID a_actor) {
  std::scoped_lock lock{g_animMutex};
  std::shared_ptr<const GameObjectCatalog> &cached = g_animCatalogs[a_actor];
  if (cached && !g_animDirty.contains(a_actor)) {
    return cached;
  }
  auto catalog = std::make_shared<GameObjectCatalog>();
  catalog->kind = GameObjectKind::kAnimEvent;
  if (const auto tags = g_animTags.find(a_actor); tags != g_animTags.end()) {
    for (const std::string &tag : tags->second) {
      GameObjectCandidate candidate;
      candidate.display = tag;
      candidate.value = AnimEventId(tag);
      catalog->candidates.push_back(std::move(candidate));
    }
  }
  catalog->sourceNote =
      std::format("{} event(s) seen so far", catalog->candidates.size());
  g_animDirty.erase(a_actor);
  cached = std::move(catalog);
  return cached;
}

namespace {
constexpr std::size_t kMaxNodeDepth = 64;
constexpr std::size_t kMaxNodes = 4096;

struct PendingNode {
  RE::NiAVObject *object = nullptr;
  std::size_t depth = 0;
};

void NoteNodeName(std::vector<std::string> &a_names, const RE::NiNode &a_node) {
  const char *name = a_node.name.c_str();
  if (name && *name) {
    a_names.emplace_back(name);
  }
}

void QueueChildren(std::vector<PendingNode> &a_pending, RE::NiNode &a_node,
                   std::size_t a_depth) {
  if (a_depth >= kMaxNodeDepth) {
    return;
  }
  for (const RE::NiPointer<RE::NiAVObject> &child : a_node.GetChildren()) {
    if (child && a_pending.size() < kMaxNodes) {
      a_pending.push_back({child.get(), a_depth + 1});
    }
  }
}
}

std::vector<std::string> NodeNamesOf(RE::FormID a_actor) {
  std::vector<std::string> names;
  RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_actor);
  RE::NiAVObject *root = actor ? actor->Get3D(false) : nullptr;
  if (!root) {
    return names;
  }
  std::vector<PendingNode> pending{{root, 0}};
  std::size_t visited = 0;
  while (!pending.empty() && visited < kMaxNodes) {
    const PendingNode next = pending.back();
    pending.pop_back();
    ++visited;
    RE::NiNode *node = next.object ? next.object->AsNode() : nullptr;
    if (!node) {
      continue;
    }
    NoteNodeName(names, *node);
    QueueChildren(pending, *node, next.depth);
  }
  return names;
}

void NoteAnimEvent(RE::FormID a_actor, std::string_view a_tag) {
  if (a_tag.empty()) {
    return;
  }
  std::scoped_lock lock{g_animMutex};
  std::set<std::string, std::less<>> &tags = g_animTags[a_actor];
  if (!tags.contains(a_tag)) {
    tags.emplace(a_tag);
    g_animDirty.insert(a_actor);
  }
}

void ForgetAnimEvents(RE::FormID a_actor) {
  std::scoped_lock lock{g_animMutex};
  g_animTags.erase(a_actor);
  g_animCatalogs.erase(a_actor);
  g_animDirty.erase(a_actor);
}

void ClearAnimEvents() {
  std::scoped_lock lock{g_animMutex};
  g_animTags.clear();
  g_animCatalogs.clear();
  g_animDirty.clear();
}

std::optional<FormKey> ResolveEditorId(std::string_view a_editorId) {
  std::scoped_lock lock{g_mutex};
  if (g_editorIds.empty()) {
    RebuildLocked();
  }
  const auto it = g_editorIds.find(Lower(a_editorId));
  if (it == g_editorIds.end()) {
    return std::nullopt;
  }
  return it->second;
}
}
