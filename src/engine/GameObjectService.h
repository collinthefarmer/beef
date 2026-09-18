#pragma once

#include "PCH.h"
#include "recipe/Recipe.h"
#include "studio/GameObjects.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
void RebuildGameObjectCatalogs();

[[nodiscard]] std::shared_ptr<const Studio::GameObjectCatalog>
GameObjectCatalogOf(Studio::GameObjectKind a_kind);

[[nodiscard]] std::shared_ptr<const Studio::GameObjectCatalog>
AnimEventCatalogOf(RE::FormID a_actor);

void NoteAnimEvent(RE::FormID a_actor, std::string_view a_tag);

void ForgetAnimEvents(RE::FormID a_actor);

[[nodiscard]] std::optional<FormKey>
ResolveEditorId(std::string_view a_editorId);
}
