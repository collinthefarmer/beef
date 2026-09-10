#include "engine/RecipeStore.h"

#include "Identity.h"
#include "PCH.h"
#include "engine/EngineForms.h"
#include "recipe/Importer.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"
#include "studio/Presets.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace BetterEnchantmentEffects {
namespace {
struct LoadedRecipe {
  Recipe recipe;
  std::filesystem::path path;
  std::vector<Diagnostic> diagnostics;
  std::shared_ptr<const SignalGraph> graph;
  bool dirty = false;
  bool transient = false;
  Studio::ReferenceCounts references{};
  Recipe saved{};
};

std::vector<LoadedRecipe> g_loaded;
std::vector<Recipe> g_recipes;
RecipeStoreStatus g_status;
Studio::MaskPresets g_presets;

std::string ReadText(const std::filesystem::path &a_path) {
  std::ifstream in(a_path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool WriteText(const std::filesystem::path &a_path, std::string_view a_text) {
  std::error_code ec;
  std::filesystem::create_directories(a_path.parent_path(), ec);
  std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
  out << a_text;
  return static_cast<bool>(out);
}

void LoadPresets() {
  g_presets = {};
  const auto path = Identity::PresetsPath();
  const auto text = ReadText(path);
  if (text.empty()) {
    logger::warn("presets: {} is missing or empty; no mask presets",
                 path.string());
    return;
  }
  auto parsed = Studio::ParsePresets(text);
  if (!parsed) {
    logger::warn("presets: {}: {}", path.string(), parsed.error());
    return;
  }
  g_presets = std::move(*parsed);
  logger::info("presets: {} mask presets", g_presets.presets.size());
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
  return !rel.empty() && rel.native()[0] != '.';
}

std::string Lower(std::string_view a_text) {
  std::string out{a_text};
  for (auto &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::unordered_map<std::string, FormKey> g_editorIds;

template <class Form> void IndexEditorIds(RE::TESDataHandler &a_handler) {
  for (auto *form : a_handler.GetFormArray<Form>()) {
    if (!form) {
      continue;
    }
    if (const auto id = EditorIdOf(*form); !id.empty()) {
      g_editorIds.emplace(Lower(id), FormKeyFor(*form));
    }
  }
}

void IndexEditorIds() {
  g_editorIds.clear();
  auto *handler = RE::TESDataHandler::GetSingleton();
  if (!handler) {
    return;
  }
  IndexEditorIds<RE::TESEffectShader>(*handler);
  IndexEditorIds<RE::EnchantmentItem>(*handler);
  IndexEditorIds<RE::EffectSetting>(*handler);
  IndexEditorIds<RE::BGSKeyword>(*handler);
  IndexEditorIds<RE::TESObjectARMO>(*handler);
  IndexEditorIds<RE::TESObjectARMA>(*handler);
  IndexEditorIds<RE::TESObjectLIGH>(*handler);
  logger::info("recipes: {} editor IDs indexed{}", g_editorIds.size(),
               TweaksEditorIdsAvailable()
                   ? " (po3's Tweaks answers the rest)"
                   : " (po3's Tweaks not loaded: only the engine's own)");
}

void ResolveForm(FormRef &a_ref, const std::string &a_id,
                 const std::string &a_where, std::vector<Diagnostic> &a_out) {
  if (a_ref.Resolved() || a_ref.text.empty()) {
    return;
  }
  if (const auto it = g_editorIds.find(Lower(a_ref.text));
      it != g_editorIds.end()) {
    a_ref.key = it->second;
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
      ResolveForm(*form, a_recipe.id, std::format("key {}", key.ToString()),
                  a_out);
    }
  }
  for (auto &signal : a_recipe.signals) {
    if (auto *efsh = Get<EfshSignal>(signal.kind)) {
      ResolveForm(efsh->record, a_recipe.id,
                  std::format("signal {}", signal.name), a_out);
    }
  }
  std::size_t index = 0;
  for (auto &output : a_recipe.outputs) {
    const auto where = std::format("output {}", index++);
    Match(
        output,
        [&](SurfaceOutput &m) {
          ResolveSelector(m.selector, a_recipe.id, where, a_out);
        },
        [&](LightOutput &l) {
          ResolveSelector(l.selector, a_recipe.id, where, a_out);
          if (l.bulb) {
            ResolveForm(*l.bulb, a_recipe.id, where, a_out);
          }
        });
  }
  for (auto &variant : a_recipe.variants) {
    const auto where = std::format("variant {}", variant.name);
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

bool HasErrors(const std::vector<Diagnostic> &a_diagnostics) {
  return std::ranges::any_of(a_diagnostics, [](const Diagnostic &d) {
    return d.severity == Severity::kError;
  });
}

void LoadFile(const std::filesystem::path &a_path) {
  const auto id = a_path.stem().string();
  auto result = ParseRecipe(ReadText(a_path), id);
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
  const auto existing = std::ranges::find(
      g_loaded, id, [](const LoadedRecipe &l) { return l.recipe.id; });
  if (existing != g_loaded.end()) {
    logger::info("recipe {}: {} replaces {}", id, a_path.string(),
                 existing->path.string());
    *existing = {std::move(*result.recipe), a_path,
                 std::move(result.diagnostics), nullptr, false};
    return;
  }
  g_loaded.push_back({std::move(*result.recipe), a_path,
                      std::move(result.diagnostics), nullptr, false});
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

void ImportMissing(const std::filesystem::path &a_folder) {
  for (auto *shader : ArmorEnchantmentShaders()) {
    if (!shader) {
      continue;
    }
    const auto record = RecordFrom(*shader);
    if (HasEffectShaderKey(record.key)) {
      continue;
    }
    auto recipe = ImportEffectShader(record);
    const auto text = SerializeRecipe(recipe);
    const auto path = a_folder / (recipe.id + ".json");
    if (!WriteText(path, text)) {
      logger::error("recipe {}: could not write {}", recipe.id, path.string());
      continue;
    }
    const auto readBack = ReadText(path);
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
    g_loaded.push_back({std::move(recipe), path, {}, nullptr, false});
  }
}

void LogKeyOwnership() {
  std::unordered_map<std::string, std::string> owner;
  for (const auto &l : g_loaded) {
    for (const auto &key : l.recipe.keys) {
      owner[key.ToString()] = l.recipe.id;
    }
  }
  for (const auto &l : g_loaded) {
    for (const auto &key : l.recipe.keys) {
      const auto &id = owner[key.ToString()];
      if (id != l.recipe.id) {
        logger::warn("recipe {}: key {} is owned by recipe {} (loaded later); "
                     "{} does not resolve by it",
                     l.recipe.id, key.ToString(), id, l.recipe.id);
      }
    }
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
  IndexEditorIds();
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
  LogKeyOwnership();
  LoadPresets();
  g_recipes.clear();
  for (auto &l : g_loaded) {
    l.references = Studio::CountReferences(l.recipe);
    l.saved = l.recipe;
    g_recipes.push_back(l.recipe);
  }
  logger::info("recipes: {} loaded, {} with errors, {} unresolved editor IDs, "
               "{} imported this session, folder {}",
               g_status.loaded, g_status.withErrors, g_status.unresolved,
               g_status.imported, std::filesystem::absolute(root, ec).string());
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
LoadedRecipe *Loaded(std::string_view a_id) noexcept {
  const auto it = std::ranges::find(g_loaded, a_id, [](const LoadedRecipe &l) {
    return std::string_view{l.recipe.id};
  });
  return it == g_loaded.end() ? nullptr : &*it;
}

void Republish(LoadedRecipe &a_loaded) {
  a_loaded.references = Studio::CountReferences(a_loaded.recipe);
  const auto index = static_cast<std::size_t>(&a_loaded - g_loaded.data());
  if (index < g_recipes.size()) {
    g_recipes[index] = a_loaded.recipe;
  } else {
    logger::error("recipe {}: the published list has {} entries for {} loaded",
                  a_loaded.recipe.id, g_recipes.size(), g_loaded.size());
  }
}

void Publish(LoadedRecipe a_loaded) {
  a_loaded.references = Studio::CountReferences(a_loaded.recipe);
  g_loaded.push_back(std::move(a_loaded));
  g_recipes.push_back(g_loaded.back().recipe);
}

void Unpublish(std::size_t a_index) {
  g_loaded.erase(g_loaded.begin() + static_cast<std::ptrdiff_t>(a_index));
  if (a_index < g_recipes.size()) {
    g_recipes.erase(g_recipes.begin() + static_cast<std::ptrdiff_t>(a_index));
  }
}
}

Recipe *MutableRecipe(std::string_view a_id) noexcept {
  auto *loaded = Loaded(a_id);
  return loaded ? &loaded->recipe : nullptr;
}

std::span<const Diagnostic> Revalidate(std::string_view a_id) {
  auto *loaded = Loaded(a_id);
  if (!loaded) {
    return {};
  }
  loaded->diagnostics = Validate(loaded->recipe);
  ResolveForms(loaded->recipe, loaded->diagnostics);
  loaded->graph.reset();
  loaded->dirty = !loaded->transient && !(loaded->recipe == loaded->saved);
  Republish(*loaded);
  return loaded->diagnostics;
}

const Studio::ReferenceCounts *ReferencesOf(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded ? &loaded->references : nullptr;
}

bool IsDirty(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded && loaded->dirty;
}

std::expected<std::filesystem::path, std::string>
SaveRecipe(std::string_view a_id) {
  auto *loaded = Loaded(a_id);
  if (!loaded) {
    return std::unexpected("not loaded");
  }
  if (loaded->transient) {
    return std::unexpected("the paint recipe is never written");
  }
  auto path = loaded->path;
  if (IsUnder(path, Identity::ImportedRecipeFolder())) {
    path = Identity::UserRecipeFolder() / (loaded->recipe.id + ".json");
    loaded->recipe.metadata.imported.clear();
  }
  Recipe written = loaded->recipe;
  std::erase_if(written.masks,
                [](const Mask &m) { return m.name == Studio::kScratchMask; });
  for (auto &output : written.outputs) {
    if (auto *material = Get<SurfaceOutput>(output)) {
      for (auto &layer : material->stack) {
        if (layer.mask && layer.mask->name == Studio::kScratchMask) {
          layer.mask.reset();
        }
      }
    }
  }
  if (!WriteText(path, SerializeRecipe(written))) {
    return std::unexpected(std::format("could not write {}", path.string()));
  }
  loaded->path = path;
  loaded->saved = loaded->recipe;
  loaded->dirty = false;
  Republish(*loaded);
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

bool RenameRecipe(std::string_view a_from, std::string_view a_to) {
  if (!IsStem(a_to)) {
    logger::warn("rename '{}': an id is a file stem (letters, digits, '-', "
                 "'_', '.') and not the paint recipe's",
                 a_to);
    return false;
  }
  if (Loaded(a_to)) {
    logger::warn("rename {} -> {}: a recipe has that id", a_from, a_to);
    return false;
  }
  auto *loaded = Loaded(a_from);
  if (!loaded || loaded->transient) {
    logger::warn("rename {}: {}", a_from,
                 loaded ? "the paint recipe keeps its name" : "not loaded");
    return false;
  }
  const auto to = Identity::UserRecipeFolder() / (std::string{a_to} + ".json");
  std::error_code ec;
  if (IsUnder(loaded->path, Identity::UserRecipeFolder()) &&
      std::filesystem::exists(loaded->path, ec)) {
    std::filesystem::rename(loaded->path, to, ec);
    if (ec) {
      logger::warn("rename {} -> {}: {} could not be moved ({}); the old file "
                   "stays",
                   a_from, a_to, loaded->path.string(), ec.message());
    }
  } else if (std::filesystem::exists(loaded->path, ec)) {
    logger::info("rename {} -> {}: {} is not the user's file and stays; it "
                 "loads again under its old id at the next start",
                 a_from, a_to, loaded->path.string());
  }
  loaded->recipe.id = std::string{a_to};
  loaded->path = to;
  Revalidate(a_to);
  logger::info("recipe {} renamed {}; saves to {}", a_from, a_to, to.string());
  return true;
}

bool NewRecipe(std::string_view a_id, RecipeKey a_key,
               std::string_view a_geometry) {
  if (!IsStem(a_id)) {
    logger::warn("new recipe '{}': an id is a file stem (letters, digits, '-', "
                 "'_', '.') and not the paint recipe's",
                 a_id);
    return false;
  }
  if (Loaded(a_id)) {
    logger::warn("new recipe '{}': a recipe has that id", a_id);
    return false;
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
  LoadedRecipe loaded{std::move(recipe),
                      Identity::UserRecipeFolder() / (id + ".json"),
                      {},
                      nullptr,
                      true};
  loaded.diagnostics = Validate(loaded.recipe);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  Publish(std::move(loaded));
  logger::info("new recipe {} keyed by {}; saves to {}", id,
               g_loaded.back().recipe.keys[0].ToString(),
               g_loaded.back().path.string());
  return true;
}

bool AddTransientRecipe(Recipe a_recipe) {
  if (a_recipe.id.empty() || Loaded(a_recipe.id)) {
    logger::warn("transient recipe '{}': a recipe has that id", a_recipe.id);
    return false;
  }
  LoadedRecipe loaded{std::move(a_recipe), {}, {}, nullptr, false, true};
  loaded.diagnostics = Validate(loaded.recipe);
  ResolveForms(loaded.recipe, loaded.diagnostics);
  for (const auto &d : loaded.diagnostics) {
    if (d.severity == Severity::kError) {
      logger::warn("transient recipe {} {}: {}", loaded.recipe.id, d.where,
                   d.message);
    }
  }
  Publish(std::move(loaded));
  return true;
}

bool DropTransientRecipe(std::string_view a_id) {
  const auto it = std::ranges::find(g_loaded, a_id, [](const LoadedRecipe &l) {
    return std::string_view{l.recipe.id};
  });
  if (it == g_loaded.end() || !it->transient) {
    return false;
  }
  Unpublish(static_cast<std::size_t>(it - g_loaded.begin()));
  return true;
}

bool IsTransient(std::string_view a_id) noexcept {
  const auto *loaded = Loaded(a_id);
  return loaded && loaded->transient;
}

bool RevertRecipe(std::string_view a_id) {
  auto *loaded = Loaded(a_id);
  if (!loaded || loaded->transient) {
    return false;
  }
  auto result = ParseRecipe(ReadText(loaded->path), loaded->recipe.id);
  if (!result.recipe) {
    logger::error("recipe {}: revert failed, {} is unreadable",
                  loaded->recipe.id, loaded->path.string());
    return false;
  }
  ResolveForms(*result.recipe, result.diagnostics);
  loaded->recipe = std::move(*result.recipe);
  loaded->diagnostics = std::move(result.diagnostics);
  loaded->graph.reset();
  loaded->saved = loaded->recipe;
  loaded->dirty = false;
  Republish(*loaded);
  return true;
}
}
