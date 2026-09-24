// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RecipeStore.h"

#include "Identity.h"
#include "PCH.h"
#include "engine/EngineForms.h"
#include "engine/GameObjectService.h"
#include "engine/RecipeFiles.h"
#include "engine/TextFile.h"
#include "recipe/DefinitionOrder.h"
#include "recipe/Importer.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"
#include "studio/Presets.h"

#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace BetterEnchantmentEffects {
namespace {
// NOLINTNEXTLINE(bugprone-exception-escape)
struct LoadedRecipe {
  Recipe recipe;
  std::filesystem::path path;
  std::vector<Diagnostic> diagnostics;
  std::shared_ptr<const SignalGraph> graph;
  bool dirty = false;
  bool transient = false;
  Studio::ReferenceCounts references{};
  Recipe saved{};
  std::vector<Diagnostic> inputDiagnostics;
};
static_assert(std::is_nothrow_move_constructible_v<Studio::ReferenceCounts> ||
              !std::is_nothrow_move_constructible_v<LoadedRecipe>);

std::vector<LoadedRecipe> g_loaded;
std::vector<Recipe> g_recipes;
RecipeStoreStatus g_status;
Studio::MaskPresets g_presets;

Diagnostic Refusal(std::string_view a_id, std::string a_message) {
  return MakeDiagnostic(Severity::kError, std::format("recipe {}", a_id),
                        std::move(a_message));
}

bool HeldBack(const LoadedRecipe &a_loaded) noexcept {
  return HasRecipeErrors(a_loaded.diagnostics);
}

void RebuildApplied() {
  g_recipes.clear();
  g_status.heldBack = 0;
  for (const LoadedRecipe &loaded : g_loaded) {
    if (HeldBack(loaded)) {
      ++g_status.heldBack;
      continue;
    }
    g_recipes.push_back(loaded.recipe);
  }
}

void LoadPresets() {
  g_presets = {};
  const auto path = Identity::PresetsPath();
  const auto text = ReadText(path);
  if (!text) {
    logger::warn("presets: {} {}; no mask presets", path.string(),
                 text.error());
    return;
  }
  if (text->empty()) {
    logger::warn("presets: {} is empty; no mask presets", path.string());
    return;
  }
  Studio::PresetsLoadResult parsed = Studio::ParsePresets(*text);
  for (const Diagnostic &d : parsed.diagnostics) {
    if (d.severity == Severity::kError) {
      logger::error("presets: {}: {}: {}", path.string(), d.where, d.message);
    } else {
      logger::warn("presets: {}: {}: {}", path.string(), d.where, d.message);
    }
  }
  if (!parsed.presets) {
    logger::warn("presets: {} could not be read; no mask presets",
                 path.string());
    return;
  }
  g_presets = std::move(*parsed.presets);
  logger::info("presets: {} mask presets from {}{}", g_presets.presets.size(),
               path.string(),
               parsed.HasErrors() ? " (some entries had errors)" : "");
}

std::vector<std::filesystem::path>
JsonFilesUnder(const std::filesystem::path &a_root) {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(a_root, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  return files;
}

bool IsUnder(const std::filesystem::path &a_path,
             const std::filesystem::path &a_folder) {
  const auto rel = a_path.lexically_relative(a_folder);
  return !rel.empty() && rel != "." && *rel.begin() != "..";
}

void ResolveForm(FormRef &a_ref, const std::string &a_id,
                 const std::string &a_where, std::vector<Diagnostic> &a_out) {
  if (a_ref.Resolved() || a_ref.text.empty()) {
    return;
  }
  if (const std::optional<FormKey> key = ResolveEditorId(a_ref.text)) {
    a_ref.key = key;
    return;
  }
  if (auto *form = RE::TESForm::LookupByEditorID(a_ref.text)) {
    a_ref.key = FormKeyFor(*form);
    return;
  }
  ++g_status.unresolved;
  const Reporter report{a_out, a_where};
  report.Warn(std::format(
      "editor ID '{}' matched no loaded form (effect shaders, enchantments "
      "and armor need po3's Tweaks for editor IDs); this key never matches",
      a_ref.text));
  logger::warn("recipe {}: {}: editor ID '{}' matched no loaded form", a_id,
               a_where, a_ref.text);
}

void ResolveSelector(Selector &a_selector, const std::string &a_id,
                     const std::string &a_where,
                     std::vector<Diagnostic> &a_out) {
  for (auto &term : a_selector.anyOf) {
    if (auto *form = term.Form()) {
      ResolveForm(*form, a_id, a_where, a_out);
    }
  }
}

void ResolveForms(Recipe &a_recipe, std::vector<Diagnostic> &a_out) {
  for (auto &key : a_recipe.keys) {
    if (auto *form = key.Form()) {
      ResolveForm(*form, a_recipe.id, KeyWhere(key), a_out);
    }
  }
  for (auto &signal : a_recipe.signals) {
    if (auto *efsh = Get<EfshSignal>(signal.kind)) {
      ResolveForm(efsh->record, a_recipe.id, SignalWhere(signal.name), a_out);
    }
  }
  std::size_t index = 0;
  for (auto &output : a_recipe.outputs) {
    const auto where = OutputWhere(index++);
    Match(
        output,
        [&](SurfaceOutput &m) {
          ResolveSelector(m.selector, a_recipe.id, where, a_out);
        },
        [&](LightOutput &l) {
          ResolveSelector(l.selector, a_recipe.id, where, a_out);
        });
  }
  for (auto &variant : a_recipe.variants) {
    const auto where = VariantWhere(variant.name);
    Match(
        variant.key,
        [&](FormRef &armor) { ResolveForm(armor, a_recipe.id, where, a_out); },
        [&](Selector &s) { ResolveSelector(s, a_recipe.id, where, a_out); });
  }
}

void LogDiagnostics(const std::string &a_id,
                    const std::vector<Diagnostic> &a_diagnostics) {
  for (const auto &d : a_diagnostics) {
    if (d.severity == Severity::kError) {
      logger::error("recipe {}: {}: {}", a_id, d.where, d.message);
    } else {
      logger::warn("recipe {}: {}: {}", a_id, d.where, d.message);
    }
  }
}

void LoadFile(const std::filesystem::path &a_path) {
  const auto id = a_path.stem().string();
  const auto text = ReadText(a_path);
  if (!text || text->empty()) {
    logger::error("recipe {}: unreadable ({})", a_path.string(),
                  text ? "empty" : text.error());
    ++g_status.withErrors;
    return;
  }
  auto result = ParseRecipe(*text, id);
  if (!result.recipe) {
    logger::error(
        "recipe {}: unreadable ({})", a_path.string(),
        result.diagnostics.empty() ? "" : result.diagnostics.front().message);
    ++g_status.withErrors;
    return;
  }
  ResolveForms(*result.recipe, result.diagnostics);
  LogDiagnostics(id, result.diagnostics);
  g_status.withErrors += HasErrors(result.diagnostics) ? 1 : 0;
  ++g_status.loaded;
  if (HasRecipeErrors(result.diagnostics)) {
    logger::error("recipe {}: held back from the applied set until its "
                  "recipe-level errors are fixed",
                  id);
  }
  std::string keys;
  for (const auto &k : result.recipe->keys) {
    keys += (keys.empty() ? "" : ", ") + k.ToString();
  }
  const auto &r = *result.recipe;
  logger::info("recipe {} loaded from {} (keys: {}; {} signals, {} curves, {} "
               "sources, {} masks, {} outputs{})",
               id, a_path.string(), keys, r.signals.size(), r.curves.size(),
               r.sources.size(), r.masks.size(), r.outputs.size(),
               r.metadata.imported.empty() ? "" : "; imported, not yet edited");
  LoadedRecipe loaded;
  loaded.recipe = std::move(*result.recipe);
  loaded.path = a_path;
  loaded.diagnostics = std::move(result.diagnostics);
  loaded.inputDiagnostics = std::move(result.inputDiagnostics);
  const auto existing = std::ranges::find(
      g_loaded, id, [](const LoadedRecipe &l) { return l.recipe.id; });
  if (existing != g_loaded.end()) {
    logger::info("recipe {}: {} replaces {}", id, a_path.string(),
                 existing->path.string());
  }
  AppendDefinition(g_loaded, std::move(loaded),
                   [](const LoadedRecipe &a_loaded) -> const std::string & {
                     return a_loaded.recipe.id;
                   });
}

std::vector<RE::TESEffectShader *> ArmorEnchantmentShaders() {
  std::vector<RE::TESEffectShader *> out;
  std::unordered_set<RE::TESEffectShader *> seen;
  auto *handler = RE::TESDataHandler::GetSingleton();
  if (!handler) {
    return out;
  }
  for (auto *enchantment : handler->GetFormArray<RE::EnchantmentItem>()) {
    if (!enchantment || enchantment->GetCastingType() !=
                            RE::MagicSystem::CastingType::kConstantEffect) {
      continue;
    }
    if (auto *shader = ShaderFor(enchantment);
        shader && seen.insert(shader).second) {
      out.push_back(shader);
    }
  }
  return out;
}

bool HasEffectShaderKey(const FormKey &a_key) {
  return std::ranges::any_of(g_loaded, [&](const LoadedRecipe &l) {
    return std::ranges::any_of(l.recipe.keys, [&](const RecipeKey &k) {
      const auto *form = k.Form();
      return k.kind == KeyKind::kEffectShader && form && form->key &&
             *form->key == a_key;
    });
  });
}

std::unordered_map<std::string_view, Recipe> LoadImportTemplates() {
  std::unordered_map<std::string_view, Recipe> templates;
  for (const std::string_view id : kImportTemplateIds) {
    const auto path = Identity::TemplateFolder() / std::format("{}.json", id);
    const auto text = ReadText(path);
    if (!text) {
      logger::error("import template {}: {} {}", id, path.string(),
                    text.error());
      continue;
    }
    LoadResult parsed = ParseRecipe(*text, id);
    for (const Diagnostic &d : parsed.diagnostics) {
      logger::error("import template {}: {}: {}", id, d.where, d.message);
    }
    if (!parsed.recipe || parsed.HasErrors()) {
      continue;
    }
    templates.emplace(id, std::move(*parsed.recipe));
  }
  return templates;
}

void ImportMissing(const std::filesystem::path &a_folder) {
  const auto templates = LoadImportTemplates();
  for (auto *shader : ArmorEnchantmentShaders()) {
    if (!shader) {
      continue;
    }
    const auto record = RecordFrom(*shader);
    if (HasEffectShaderKey(record.key)) {
      continue;
    }
    const std::string_view templateId = ImportTemplateId(record);
    const auto found = templates.find(templateId);
    if (found == templates.end()) {
      logger::error(
          "recipe {}: the {} import template did not load; efsh {:08X} "
          "not imported",
          RecipeIdFor(record), templateId, shader->GetFormID());
      continue;
    }
    auto recipe = ImportEffectShader(record, found->second);
    const auto text = SerializeRecipe(recipe);
    const auto path = a_folder / (recipe.id + ".json");
    if (!WriteText(path, text)) {
      logger::error("recipe {}: could not write {}", recipe.id, path.string());
      continue;
    }
    const std::string readBack = ReadText(path).value_or("");
    auto parsed = ParseRecipe(readBack, recipe.id);
    const bool identical = readBack == text && parsed.recipe &&
                           *parsed.recipe == recipe && !parsed.HasErrors();
    if (identical) {
      logger::info(
          "imported recipe {} for efsh {:08X} ({}) -> {}: reads back identical",
          recipe.id, shader->GetFormID(), record.fillTexture, path.string());
    } else {
      logger::error("imported recipe {} for efsh {:08X} -> {}: READ-BACK "
                    "MISMATCH (text {}, parsed {}, errors {})",
                    recipe.id, shader->GetFormID(), path.string(),
                    readBack == text, parsed.recipe && *parsed.recipe == recipe,
                    parsed.HasErrors());
      LogDiagnostics(recipe.id, parsed.diagnostics);
    }
    for (auto &key : recipe.keys) {
      if (auto *form = key.Form()) {
        form->key = record.key;
      }
    }
    for (auto &signal : recipe.signals) {
      if (auto *efsh = Get<EfshSignal>(signal.kind)) {
        efsh->record.key = record.key;
      }
    }
    ++g_status.imported;
    ++g_status.loaded;
    LoadedRecipe loaded;
    loaded.recipe = std::move(recipe);
    loaded.path = path;
    g_loaded.push_back(std::move(loaded));
  }
}

}

RecipeStoreStatus LoadRecipes() {
  g_loaded.clear();
  g_recipes.clear();
  g_status = {};
  const auto root = Identity::RecipeRoot();
  const auto user = Identity::UserRecipeFolder();
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    logger::error("recipes: cannot create {} ({})", root.string(),
                  ec.message());
  }
  RebuildGameObjectCatalogs();
  std::vector<std::filesystem::path> shipped, saved;
  for (const auto &path : JsonFilesUnder(root)) {
    (IsUnder(path, user) ? saved : shipped).push_back(path);
  }
  for (const auto &path : shipped) {
    LoadFile(path);
  }
  for (const auto &path : saved) {
    LoadFile(path);
  }
  ImportMissing(Identity::ImportedRecipeFolder());
  LoadPresets();
  for (auto &l : g_loaded) {
    l.references = Studio::CountReferences(l.recipe);
    l.saved = l.recipe;
  }
  RebuildApplied();
  logger::info("recipes: {} loaded, {} with errors, {} held back, {} "
               "unresolved editor IDs, {} imported this session, folder {}",
               g_status.loaded, g_status.withErrors, g_status.heldBack,
               g_status.unresolved, g_status.imported,
               std::filesystem::absolute(root, ec).string());
  return g_status;
}

RecipeStoreStatus GetRecipeStoreStatus() noexcept { return g_status; }

std::span<const Recipe> LoadedRecipes() noexcept { return g_recipes; }

const Studio::MaskPresets &LoadedPresets() noexcept { return g_presets; }

std::optional<RecipeOrigin> OriginOf(const Recipe &a_recipe) noexcept {
  for (const auto &l : g_loaded) {
    if (l.recipe.id == a_recipe.id) {
      return RecipeOrigin{l.path, l.diagnostics};
    }
  }
  return std::nullopt;
}

std::shared_ptr<const SignalGraph> GraphFor(const Recipe &a_recipe) {
  for (auto &l : g_loaded) {
    if (l.recipe.id == a_recipe.id) {
      if (!l.graph) {
        l.graph = std::make_shared<const SignalGraph>(
            SignalGraph::Compile(a_recipe.signals, a_recipe.curves));
      }
      return l.graph;
    }
  }
  return nullptr;
}

namespace {
std::optional<std::size_t> LoadedIndex(std::string_view a_id) noexcept {
  const auto it = std::ranges::find(g_loaded, a_id, [](const LoadedRecipe &l) {
    return std::string_view{l.recipe.id};
  });
  if (it == g_loaded.end()) {
    return std::nullopt;
  }
  return IndexOf(g_loaded, it);
}

LoadedRecipe *Loaded(std::string_view a_id) noexcept {
  const auto index = LoadedIndex(a_id);
  return index ? &g_loaded[*index] : nullptr;
}

void Republish(std::size_t a_index) {
  if (a_index >= g_loaded.size()) {
    logger::error("recipe store: entry {} cannot be republished; {} loaded",
                  a_index, g_loaded.size());
    return;
  }
  LoadedRecipe &loaded = g_loaded[a_index];
  loaded.references = Studio::CountReferences(loaded.recipe);
  RebuildApplied();
}

void Publish(LoadedRecipe a_loaded) {
  a_loaded.references = Studio::CountReferences(a_loaded.recipe);
  g_loaded.push_back(std::move(a_loaded));
  RebuildApplied();
}

void Unpublish(std::size_t a_index) {
  if (a_index >= g_loaded.size()) {
    return;
  }
  g_loaded.erase(g_loaded.begin() + static_cast<std::ptrdiff_t>(a_index));
  RebuildApplied();
}
}

Recipe *MutableRecipe(std::string_view a_id) noexcept {
  auto *loaded = Loaded(a_id);
  return loaded ? &loaded->recipe : nullptr;
}

std::span<const Diagnostic> RefreshRecipeDerivedState(std::string_view a_id) {
  const auto index = LoadedIndex(a_id);
  if (!index) {
    return {};
  }
  LoadedRecipe &loaded = g_loaded[*index];
  loaded.diagnostics = Validate(loaded.recipe, loaded.inputDiagnostics);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  loaded.graph.reset();
  loaded.dirty = !loaded.transient && !(loaded.recipe == loaded.saved);
  Republish(*index);
  return loaded.diagnostics;
}

const Studio::ReferenceCounts *ReferencesOf(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded ? &loaded->references : nullptr;
}

bool IsDirty(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded && loaded->dirty;
}

std::expected<std::filesystem::path, Diagnostic>
SaveRecipe(std::string_view a_id) {
  const auto index = LoadedIndex(a_id);
  if (!index) {
    return std::unexpected(Refusal(a_id, "not loaded"));
  }
  LoadedRecipe *loaded = &g_loaded[*index];
  if (loaded->transient) {
    return std::unexpected(Refusal(a_id, "the paint recipe is never written"));
  }
  auto path = loaded->path;
  const bool promoteImported = IsUnder(path, Identity::ImportedRecipeFolder());
  if (!IsUnder(path, Identity::UserRecipeFolder())) {
    path = Identity::UserRecipeFolder() / (loaded->recipe.id + ".json");
  }
  auto written = WriteRecipeFile(path, loaded->recipe, promoteImported);
  if (!written) {
    return std::unexpected(written.error());
  }
  loaded->recipe = std::move(*written);
  loaded->path = path;
  loaded->saved = loaded->recipe;
  loaded->dirty = false;
  loaded->inputDiagnostics.clear();
  RefreshRecipeDerivedState(a_id);
  logger::info("recipe {} saved to {}", loaded->recipe.id, path.string());
  return path;
}

namespace {
bool IsStem(std::string_view a_id) {
  return !a_id.empty() && a_id != Studio::kPaintRecipe &&
         std::ranges::all_of(a_id,
                             [](char c) {
                               return std::isalnum(
                                          static_cast<unsigned char>(c)) ||
                                      c == '-' || c == '_' || c == '.';
                             }) &&
         a_id[0] != '.';
}
}

std::optional<Diagnostic> RenameRecipe(std::string_view a_from,
                                       std::string_view a_to) {
  const auto refuse = [&](std::string a_message) {
    logger::warn("rename {} -> {}: {}", a_from, a_to, a_message);
    return Refusal(a_from, std::move(a_message));
  };
  if (!IsStem(a_to)) {
    return refuse(std::format("'{}' is not an id; an id is a file stem "
                              "(letters, digits, '-', '_', '.') and not the "
                              "paint recipe's",
                              a_to));
  }
  if (Loaded(a_to)) {
    return refuse(std::format("a recipe named '{}' already exists", a_to));
  }
  auto *loaded = Loaded(a_from);
  if (!loaded || loaded->transient) {
    return refuse(loaded ? "the paint recipe keeps its name" : "not loaded");
  }
  const auto to = Identity::UserRecipeFolder() / (std::string{a_to} + ".json");
  const bool owned = IsUnder(loaded->path, Identity::UserRecipeFolder());
  if (const auto problem = RenameRecipeFile(loaded->path, to, a_from, owned)) {
    return refuse(problem->message);
  }
  if (!owned) {
    logger::info("rename {} -> {}: {} is not the user's file and stays; it "
                 "loads again under its old id at the next start",
                 a_from, a_to, loaded->path.string());
  }
  loaded->recipe.id = std::string{a_to};
  if (owned) {
    loaded->saved.id = std::string{a_to};
  }
  loaded->path = to;
  RefreshRecipeDerivedState(a_to);
  logger::info("recipe {} renamed {}; saves to {}", a_from, a_to, to.string());
  return std::nullopt;
}

std::optional<Diagnostic> DeleteRecipe(std::string_view a_id) {
  const auto refuse = [&](std::string a_message) {
    logger::warn("delete {}: {}", a_id, a_message);
    return Refusal(a_id, std::move(a_message));
  };
  const auto index = LoadedIndex(a_id);
  if (!index) {
    return refuse("not loaded");
  }
  const LoadedRecipe &loaded = g_loaded[*index];
  if (loaded.transient) {
    return refuse("the paint recipe is never deleted");
  }
  const bool owned = IsUnder(loaded.path, Identity::UserRecipeFolder());
  if (const auto problem = DeleteRecipeFile(loaded.path, a_id, owned)) {
    return refuse(problem->message);
  }
  if (!owned) {
    logger::info("delete {}: {} is not the user's file and stays on disk; it "
                 "loads again under its id at the next start",
                 a_id, loaded.path.string());
  }
  logger::info("recipe {} deleted", a_id);
  Unpublish(*index);
  return std::nullopt;
}

std::optional<Diagnostic> DuplicateRecipe(std::string_view a_from,
                                          std::string_view a_to) {
  const auto refuse = [&](std::string a_message) {
    logger::warn("duplicate {} -> {}: {}", a_from, a_to, a_message);
    return Refusal(a_from, std::move(a_message));
  };
  if (!IsStem(a_to)) {
    return refuse(std::format("'{}' is not an id; an id is a file stem "
                              "(letters, digits, '-', '_', '.') and not the "
                              "paint recipe's",
                              a_to));
  }
  if (Loaded(a_to)) {
    return refuse(std::format("a recipe named '{}' already exists", a_to));
  }
  const auto *source = Loaded(a_from);
  if (!source || source->transient) {
    return refuse(source ? "the paint recipe is not duplicated" : "not loaded");
  }
  Recipe copy = source->recipe;
  copy.id = std::string{a_to};
  copy.metadata.name = copy.id;
  copy.metadata.imported.clear();
  LoadedRecipe loaded;
  loaded.recipe = std::move(copy);
  loaded.path = Identity::UserRecipeFolder() / (std::string{a_to} + ".json");
  loaded.dirty = true;
  loaded.diagnostics = Validate(loaded.recipe);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  Publish(std::move(loaded));
  logger::info("recipe {} duplicated to {}; saves to {}", a_from, a_to,
               g_loaded.back().path.string());
  return std::nullopt;
}

std::optional<Diagnostic> NewRecipe(std::string_view a_id, RecipeKey a_key,
                                    std::string_view a_geometry) {
  const auto refuse = [&](std::string a_message) {
    logger::warn("new recipe '{}': {}", a_id, a_message);
    return Refusal(a_id, std::move(a_message));
  };
  if (!IsStem(a_id)) {
    return refuse("an id is a file stem (letters, digits, '-', '_', '.') and "
                  "not the paint recipe's");
  }
  if (Loaded(a_id)) {
    return refuse("a recipe has that id");
  }
  const std::string id{a_id};
  Recipe recipe;
  recipe.id = id;
  recipe.metadata.name = recipe.id;
  recipe.keys.push_back(std::move(a_key));
  if (!a_geometry.empty()) {
    Selector selector;
    selector.anyOf.push_back(
        SelectorClause{SelectorKind::kGeometry, std::string{a_geometry}});
    [[maybe_unused]] const auto refused = Studio::Apply(
        recipe, Studio::AddOutput{Surface::kMaterial, Slot::kEmissive,
                                  std::move(selector)});
  }
  LoadedRecipe loaded;
  loaded.recipe = std::move(recipe);
  loaded.path = Identity::UserRecipeFolder() / (id + ".json");
  loaded.dirty = true;
  loaded.diagnostics = Validate(loaded.recipe);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  Publish(std::move(loaded));
  logger::info("new recipe {} keyed by {}; saves to {}", id,
               g_loaded.back().recipe.keys[0].ToString(),
               g_loaded.back().path.string());
  return std::nullopt;
}

std::optional<Diagnostic> AddTransientRecipe(Recipe a_recipe) {
  if (a_recipe.id.empty()) {
    logger::warn("transient recipe: no id");
    return Refusal(a_recipe.id, "a transient recipe needs an id");
  }
  if (Loaded(a_recipe.id)) {
    logger::warn("transient recipe '{}': a recipe has that id", a_recipe.id);
    return Refusal(a_recipe.id, "a recipe has that id");
  }
  LoadedRecipe loaded;
  loaded.recipe = std::move(a_recipe);
  loaded.transient = true;
  loaded.diagnostics = Validate(loaded.recipe);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  for (const auto &d : loaded.diagnostics) {
    if (d.severity == Severity::kError) {
      logger::warn("transient recipe {} {}: {}", loaded.recipe.id, d.where,
                   d.message);
    }
  }
  Publish(std::move(loaded));
  return std::nullopt;
}

std::optional<Diagnostic> DropTransientRecipe(std::string_view a_id) {
  const auto index = LoadedIndex(a_id);
  if (!index) {
    return Refusal(a_id, "not loaded");
  }
  if (!g_loaded[*index].transient) {
    return Refusal(a_id, "not a transient recipe");
  }
  Unpublish(*index);
  return std::nullopt;
}

bool IsTransient(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded && loaded->transient;
}

std::optional<Diagnostic> RevertRecipe(std::string_view a_id) {
  const auto index = LoadedIndex(a_id);
  if (!index) {
    return Refusal(a_id, "not loaded");
  }
  if (g_loaded[*index].transient) {
    return Refusal(a_id, "the paint recipe has no file to revert to");
  }
  LoadedRecipe &loaded = g_loaded[*index];
  auto read = ReadRecipeFile(loaded.path, loaded.recipe.id);
  if (!read) {
    logger::error("recipe {}: revert failed, {}", a_id, read.error().message);
    return read.error();
  }
  LoadResult result = std::move(*read);
  ResolveForms(*result.recipe, result.diagnostics);
  loaded.recipe = std::move(*result.recipe);
  loaded.diagnostics = std::move(result.diagnostics);
  loaded.inputDiagnostics = std::move(result.inputDiagnostics);
  loaded.graph.reset();
  loaded.saved = loaded.recipe;
  loaded.dirty = false;
  Republish(*index);
  return std::nullopt;
}
}
