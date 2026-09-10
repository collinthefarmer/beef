#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/RecipeStore.h"
#include "render/Compositor.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <random>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects {
void Manager::EditRecipe(std::string a_id, Studio::EditBatch a_edits) {
  PostTask([this, id = std::move(a_id), edits = std::move(a_edits)] {
    ApplyEdits(id, edits);
  });
}

void Manager::ApplyEdits(const std::string &a_id,
                         const Studio::EditBatch &a_edits) {
  bool keysChanged = false;
  WithRecipeRetired(a_id, [&] {
    Recipe *recipe = MutableRecipe(a_id);
    if (!recipe) {
      logger::warn("edit: recipe {} is not loaded", a_id);
      return;
    }
    const Recipe before = *recipe;
    if (const std::optional<Diagnostic> problem =
            Studio::Apply(*recipe, a_edits)) {
      logger::warn("edit refused: {} ({}: {})", Studio::Describe(a_edits),
                   problem->where, problem->message);
      return;
    }
    keysChanged = recipe->keys != before.keys;
    if (!(*recipe == before)) {
      histories_[a_id].Push(before);
    }
    for (const Diagnostic &d : Revalidate(a_id)) {
      if (d.severity == Severity::kError) {
        logger::error("recipe {} {}: {}", a_id, d.where, d.message);
      } else {
        logger::warn("recipe {} {}: {}", a_id, d.where, d.message);
      }
    }
  });
  if (keysChanged) {
    QueueLoadedActorRefreshes();
  }
}

void Manager::RestoreRecipe(const std::string &a_id, bool a_redo) {
  bool keysChanged = false;
  WithRecipeRetired(a_id, [&] {
    Recipe *recipe = MutableRecipe(a_id);
    const auto history = histories_.find(a_id);
    if (!recipe || history == histories_.end()) {
      return;
    }
    std::optional<Recipe> restored =
        a_redo ? history->second.Redo(*recipe) : history->second.Undo(*recipe);
    if (!restored) {
      return;
    }
    keysChanged = restored->keys != recipe->keys;
    *recipe = std::move(*restored);
    recipe->id = a_id;
    Revalidate(a_id);
  });
  if (keysChanged) {
    QueueLoadedActorRefreshes();
  }
}

void Manager::UndoRecipe(std::string a_id) {
  PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, false); });
}

void Manager::RedoRecipe(std::string a_id) {
  PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, true); });
}

void Manager::SaveRecipe(std::string a_id) {
  PostTask([this, id = std::move(a_id)] {
    WithRecipeRetired(id, [&] {
      const Recipe *recipe = MutableRecipe(id);
      const Recipe before = recipe ? *recipe : Recipe{};
      const std::expected<std::filesystem::path, std::string> saved =
          BetterEnchantmentEffects::SaveRecipe(id);
      if (!saved) {
        logger::error("recipe {}: save failed ({})", id, saved.error());
        return;
      }
      if (recipe && !(*recipe == before)) {
        histories_[id].Push(before);
      }
    });
  });
}

void Manager::RevertRecipe(std::string a_id) {
  PostTask([this, id = std::move(a_id)] {
    bool keysChanged = false;
    WithRecipeRetired(id, [&] {
      Recipe *recipe = MutableRecipe(id);
      if (!recipe) {
        return;
      }
      const Recipe before = *recipe;
      if (BetterEnchantmentEffects::RevertRecipe(id)) {
        keysChanged = recipe->keys != before.keys;
        if (!(*recipe == before)) {
          histories_[id].Push(before);
        }
        logger::info("recipe {}: reverted to its file", id);
      }
    });
    if (keysChanged) {
      QueueLoadedActorRefreshes();
    }
  });
}

void Manager::ReloadRecipes() {
  PostTask([this] {
    WithListMoved([&] {
      histories_.clear();
      LoadRecipes();
      paintReturn_ = {};
      const std::span<const Recipe> loaded = LoadedRecipes();
      for (const std::string &id : view_.RecipeIDs()) {
        if (std::ranges::find(loaded, id, &Recipe::id) == loaded.end()) {
          view_.ForgetRecipe(id);
        }
      }
    });
  });
}

void Manager::NewRecipe(std::string a_id, RecipeKey a_key,
                        std::string a_geometry) {
  PostTask([this, id = std::move(a_id), key = std::move(a_key),
            geometry = std::move(a_geometry)] {
    WithListMoved([&] {
      [[maybe_unused]] const bool made =
          BetterEnchantmentEffects::NewRecipe(id, std::move(key), geometry);
    });
  });
}

void Manager::RenameRecipe(std::string a_from, std::string a_to) {
  PostTask([this, from = std::move(a_from), to = std::move(a_to)] {
    WithListMoved([&] {
      if (!BetterEnchantmentEffects::RenameRecipe(from, to)) {
        return;
      }
      if (auto node = histories_.extract(from)) {
        node.key() = to;
        node.mapped().Rename(to);
        histories_.insert(std::move(node));
      }
      view_.RenameRecipe(from, to);
      if (paintReturn_.recipeID == from) {
        paintReturn_.recipeID = to;
      }
    });
  });
}

void Manager::BeginPaint(std::string a_active, RecipeKey a_key,
                         Surface a_surface) {
  PostTask([this, active = std::move(a_active), key = std::move(a_key),
            a_surface] {
    const std::span<const Recipe> loaded = LoadedRecipes();
    const auto it = std::ranges::find(loaded, active, &Recipe::id);
    if (it == loaded.end()) {
      logger::warn("paint: recipe {} is not loaded", active);
      return;
    }
    Recipe paint = Studio::PaintRecipe(*it, key, a_surface);
    WithListMoved([&] {
      if (IsTransient(Studio::kPaintRecipe)) {
        [[maybe_unused]] const bool dropped =
            DropTransientRecipe(Studio::kPaintRecipe);
      }
      if (!AddTransientRecipe(std::move(paint))) {
        if (view_.isolateRecipe == Studio::kPaintRecipe) {
          view_.isolateRecipe = paintReturn_.recipeID;
          view_.isolateOutput = paintReturn_.output;
          view_.isolateLayer = paintReturn_.layer;
          view_.isolatedBySolo = paintReturn_.bySolo;
          paintReturn_ = {};
        }
        return;
      }
      histories_.erase(std::string{Studio::kPaintRecipe});
      logger::info("paint: previewing {} on the {} through the paint recipe, "
                   "keyed by {}",
                   active, SurfaceName(a_surface), key.ToString());
      if (view_.isolateRecipe != Studio::kPaintRecipe) {
        paintReturn_ = {view_.isolateRecipe, view_.isolateOutput,
                        view_.isolateLayer, view_.isolatedBySolo};
      }
      view_.isolateRecipe = std::string{Studio::kPaintRecipe};
      view_.isolateOutput = -1;
      view_.isolateLayer = -1;
      view_.isolatedBySolo = false;
    });
  });
}

void Manager::SetPaintSurface(Surface a_surface) {
  EditRecipe(std::string{Studio::kPaintRecipe},
             Studio::EditBatch{Studio::PaintSurfaceEdits(a_surface)});
}

void Manager::KeepPaint(std::string a_active, std::string a_name) {
  PostTask([this, active = std::move(a_active), name = std::move(a_name)] {
    const std::span<const Recipe> loaded = LoadedRecipes();
    const auto paint = std::ranges::find(
        loaded, std::string{Studio::kPaintRecipe}, &Recipe::id);
    const auto target = std::ranges::find(loaded, active, &Recipe::id);
    if (paint == loaded.end() || target == loaded.end()) {
      logger::warn("keep: the paint recipe or {} is not loaded", active);
      return;
    }
    const Studio::EditBatch edits{Studio::KeepEdits(*paint, *target, name)};
    ApplyEdits(active, edits);
    logger::info("keep: mask {} written into {} ({} edit(s))", name, active,
                 edits.edits.size());
  });
  EndPaint();
}

void Manager::EndPaint() {
  PostTask([this] {
    WithListMoved([&] {
      if (view_.isolateRecipe == Studio::kPaintRecipe) {
        view_.isolateRecipe = paintReturn_.recipeID;
        view_.isolateOutput = paintReturn_.output;
        view_.isolateLayer = paintReturn_.layer;
        view_.isolatedBySolo = paintReturn_.bySolo;
        paintReturn_ = {};
      }
      view_.ForgetRecipe(Studio::kPaintRecipe);
      [[maybe_unused]] const bool dropped =
          DropTransientRecipe(Studio::kPaintRecipe);
      histories_.erase(std::string{Studio::kPaintRecipe});
    });
  });
}

void Manager::Fire(RE::FormID a_actorID, const EventRecord &a_event) {
  const auto it = applied_.find(a_actorID);
  if (it == applied_.end()) {
    return;
  }
  for (LiveInstance &instance : it->second.instances) {
    if (instance.signals) {
      instance.signals->Fire(a_event, instance.lastTime);
    }
  }
}

void Manager::QueueEvent(RE::FormID a_actorID, EventRecord a_event) {
  if (a_actorID == 0) {
    return;
  }
  PostTask([this, a_actorID, event = std::move(a_event)] {
    Fire(a_actorID, event);
  });
}

void Manager::FireAt(RE::FormID a_actorID, std::string a_event,
                     std::string a_node, Vec3 a_offset, float a_random,
                     float a_value) {
  PostTask([this, a_actorID, event = std::move(a_event),
            node = std::move(a_node), a_offset, a_random, a_value] {
    EventRecord record;
    record.id = event;
    record.payload.value = a_value;
    record.payload.node = node;
    RE::Actor *actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
    RE::NiAVObject *root = actor ? actor->Get3D(false) : nullptr;
    if (!node.empty() && root) {
      if (RE::NiAVObject *object =
              root->GetObjectByName(RE::BSFixedString{node})) {
        const RE::NiPoint3 &at = object->world.translate;
        Vec3 position{at.x + a_offset.x, at.y + a_offset.y, at.z + a_offset.z};
        if (a_random > 0.0f) {
          static std::mt19937 gen{std::random_device{}()};
          std::uniform_real_distribution<float> spread{-a_random, a_random};
          position.x += spread(gen);
          position.y += spread(gen);
          position.z += spread(gen);
        }
        record.payload.position = position;
      } else {
        logger::warn("fire {}: node '{}' is not on the actor", event, node);
      }
    }
    Fire(a_actorID, record);
  });
}

void Manager::RequestMesh(RE::FormID a_actorID, std::string a_geometry) {
  PostTask([this, a_actorID, name = std::move(a_geometry)] {
    const auto it = applied_.find(a_actorID);
    if (it == applied_.end()) {
      return;
    }
    for (LivePiece &piece : it->second.pieces) {
      for (LiveGeometry &bound : piece.geometries) {
        if (bound.name != name || bound.lost) {
          continue;
        }
        Compositor *compositor = Compositor::GetSingleton();
        if (const std::expected<std::shared_ptr<MeshEntry>, std::string> mesh =
                compositor->MeshOf(bound.geometry.get());
            !mesh) {
          logger::warn("mesh '{}': {}", name, mesh.error());
        }
        if (const Compositor::MaterialRecord &material =
                compositor->AnalyseMaterial(bound.inputs.material);
            !material.sample) {
          logger::warn("material of '{}': {}", name, material.problem);
        }
      }
    }
  });
}
}
