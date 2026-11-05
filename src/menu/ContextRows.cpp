// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/ContextRows.h"

#include "Core.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "menu/RecipeActions.h"
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Forms.h"
#include "studio/GameObjects.h"
#include "studio/Intent.h"
#include "studio/MenuState.h"
#include "studio/NameEdit.h"
#include "studio/Names.h"
#include "studio/RecipeSections.h"
#include "studio/Selection.h"
#include "studio/SelectorEdit.h"
#include "studio/Snapshot.h"
#include "studio/View.h"
#include "studio/WornPieces.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {

void DrawUnappliedEntries(const Studio::Snapshot &a_snapshot) {
  if (a_snapshot.unappliedRecipes.empty()) {
    return;
  }
  ImGui::SeparatorText("Not applied");
  for (const Studio::UnappliedRecipeRow &row : a_snapshot.unappliedRecipes) {
    Dim(std::format("{}  {}", row.id, Studio::UnappliedSummary(row)));
  }
}

void RecipeCombo(const Frame &a_frame, const char *a_label) {
  const std::string preview = a_frame.recipe ? RecipeLabel(*a_frame.recipe)
                                             : SelectionOf(a_frame).recipeID;
  if (!ImGui::BeginCombo(a_label,
                         preview.empty() ? "Choose recipe" : preview.c_str())) {
    return;
  }
  for (const Studio::RecipeSection &section :
       Studio::RecipeSectionsOf(a_frame.snapshot->loadedRecipes)) {
    ImGui::SeparatorText(section.folder.c_str());
    for (const std::string &id : section.ids) {
      const bool selected = id == SelectionOf(a_frame).recipeID;
      if (ImGui::Selectable(id.c_str(), selected)) {
        Studio::Post(*a_frame.intents, Studio::PickRecipe{id, true});
      }
    }
  }
  DrawUnappliedEntries(*a_frame.snapshot);
  ImGui::EndCombo();
}

std::string KeyValueText(const RecipeKey &a_key) {
  if (const FormRef *form = a_key.Form()) {
    return form->text;
  }
  return std::string{a_key.Glob()};
}

std::string KeyDescription([[maybe_unused]] const RecipeKey &a_key) {
  return std::string{};
}

void DrawKeyRows(const Studio::RecipeRow &a_recipe, Studio::Intents &a_out,
                 Table &a_table) {
  for (std::size_t i = 0; i < a_recipe.keys.size(); ++i) {
    const auto &key = a_recipe.keys[i];
    ImGui::PushID(static_cast<int>(i));
    a_table.Cell();
    TextOrDash(KeyKindName(key.kind));
    a_table.Cell();
    TextOrDash(KeyValueText(key));
    a_table.Cell();
    TextOrDash(KeyDescription(key));
    a_table.Cell();
    Disabled(a_recipe.keys.size() == 1, [&]() {
      if (RemoveButton(0)) {
        Studio::Post(a_out, a_recipe.id, Studio::RemoveKey{key});
      }
    });
    ImGui::PopID();
  }
}

struct KeyCandidate {
  RecipeKey key;
  std::string label;
};

std::vector<KeyCandidate>
CollectKeyCandidates(const Studio::PieceRow &a_piece,
                     const Studio::RecipeRow &a_recipe) {
  std::vector<KeyCandidate> candidates;
  const auto offer = [&](const RecipeKey &a_key, std::string a_label) {
    if (std::ranges::contains(a_recipe.keys, a_key)) {
      return;
    }
    if (std::ranges::any_of(candidates, [&](const KeyCandidate &a_have) {
          return a_have.key == a_key;
        })) {
      return;
    }
    candidates.push_back({a_key, std::move(a_label)});
  };
  for (const auto &choice : a_piece.keys) {
    offer(RecipeKeyOf(choice.key, choice.text),
          std::format("{}: {}", KeyKindName(choice.key.kind), choice.text));
  }
  return candidates;
}

struct KeyCatalogKind {
  KeyKind key;
  Studio::GameObjectKind catalog;
};
constexpr KeyCatalogKind kKeyCatalogKinds[]{
    {KeyKind::kKeyword, Studio::GameObjectKind::kKeyword},
    {KeyKind::kEnchantment, Studio::GameObjectKind::kEnchantment},
    {KeyKind::kMagicEffect, Studio::GameObjectKind::kMagicEffect},
    {KeyKind::kEffectShader, Studio::GameObjectKind::kEffectShader},
    {KeyKind::kArmor, Studio::GameObjectKind::kArmor},
};
constexpr std::size_t kKeyResultCap = 40;

std::optional<RecipeKey> DrawKeyCatalogRows(const Frame &a_frame,
                                            std::string_view a_filter,
                                            int &a_id, std::size_t &a_shown) {
  std::optional<RecipeKey> chosen;
  if (a_filter.empty() || a_frame.snapshot == nullptr) {
    return chosen;
  }
  for (const KeyCatalogKind &kind : kKeyCatalogKinds) {
    const auto &catalog = a_frame.snapshot->catalogs[IndexOf(kind.catalog)];
    const std::string prefix = std::format("{}: ", KeyKindName(kind.key));
    if (const Studio::GameObjectCandidate *picked =
            DrawCandidateRows({.filter = a_filter,
                               .catalog = catalog.get(),
                               .prefix = prefix,
                               .cap = kKeyResultCap},
                              a_id, a_shown)) {
      chosen = RecipeKey{kind.key, FormRef::From(picked->value)};
    }
  }
  return chosen;
}

void DrawKeyAdd(const Frame &a_frame, const Studio::PieceRow &a_piece,
                const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  NextItemWidth(Studio::Width::Fill());
  if (!ImGui::BeginCombo("##add-key", "add a key")) {
    return;
  }
  const std::vector<KeyCandidate> candidates =
      CollectKeyCandidates(a_piece, a_recipe);
  const std::string_view filter =
      LiveTextField("add-key-filter", "filter, or type an editor id",
                    Studio::Width::Fill(), 1.0f);
  int id = 0;
  bool any = false;
  const auto add = [&](const RecipeKey &a_key, const std::string &a_label) {
    ImGui::PushID(id++);
    if (ImGui::Selectable(a_label.c_str(), false)) {
      Studio::Post(a_out, a_recipe.id, Studio::AddKey{a_key});
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopID();
    any = true;
  };
  for (const KeyCandidate &candidate : candidates) {
    if (Studio::NameMatches(candidate.label, filter)) {
      add(candidate.key, candidate.label);
    }
  }
  std::size_t shown = 0;
  if (const std::optional<RecipeKey> key =
          DrawKeyCatalogRows(a_frame, filter, id, shown)) {
    Studio::Post(a_out, a_recipe.id, Studio::AddKey{*key});
    ImGui::CloseCurrentPopup();
  }
  any = any || shown > 0;
  if (shown >= kKeyResultCap) {
    Dim("refine your search for more");
  }
  if (!filter.empty()) {
    if (any) {
      ImGui::Separator();
    }
    RecipeKey key;
    key.kind = KeyKind::kKeyword;
    key.operand = FormRef::From(filter);
    add(key, std::format("Add keyword \"{}\"", filter));
  } else if (!any) {
    Dim("type to search keywords, forms and enchantments");
  }
  ImGui::EndCombo();
}

void PostKeywordKeys(const Studio::RecipeRow &a_recipe,
                     std::span<const RecipeKey> a_keys,
                     Studio::Intents &a_out) {
  std::vector<Studio::RecipeEdit> edits;
  for (const RecipeKey &key : a_keys) {
    edits.emplace_back(Studio::AddKey{key});
  }
  Studio::Post(a_out, Studio::EditRecipe{a_recipe.id, std::move(edits)});
}

void DrawEquippedRow(const Studio::WornPieceRow &a_piece,
                     const Studio::RecipeRow &a_recipe,
                     Studio::Intents &a_out) {
  const std::vector<RecipeKey> keys =
      Studio::KeywordKeysToAdd(a_recipe.keys, a_piece.keys);
  const std::string label = Studio::KeywordOfferLabel(a_piece, keys.size());
  Disabled(keys.empty(), [&]() {
    if (ImGui::Selectable(label.c_str(), false) && !keys.empty()) {
      PostKeywordKeys(a_recipe, keys, a_out);
      ImGui::CloseCurrentPopup();
    }
  });
}

void AddFromEquippedButton(const Frame &a_frame,
                           const Studio::RecipeRow &a_recipe,
                           Studio::Intents &a_out) {
  if (ImGui::Button("Add from Equipped")) {
    ImGui::OpenPopup("add-from-equipped");
  }
  Tooltip("add every keyword of a worn piece as a key");
  if (!ImGui::BeginPopup("add-from-equipped")) {
    return;
  }
  const std::vector<Studio::WornPieceRow> &worn = a_frame.snapshot->worn;
  if (worn.empty()) {
    Dim("the viewed actor wears no armor");
  }
  for (std::size_t i = 0; i < worn.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    DrawEquippedRow(worn[i], a_recipe, a_out);
    ImGui::PopID();
  }
  ImGui::EndPopup();
}

void DrawKeys(const Frame &a_frame, const Studio::PieceRow &a_piece,
              const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  auto table = Table::Begin("keys",
                            {{"Kind", Studio::Width::Fit()},
                             {"Value", Studio::Width::Fill()},
                             {"Description", Studio::Width::Fill()},
                             {"", Studio::Width::Px(RowButtonWidth())}},
                            Studio::kRelationTable);
  if (!table.Open()) {
    return;
  }
  DrawKeyRows(a_recipe, a_out, table);
  table.Cell();
  table.Cell();
  DrawKeyAdd(a_frame, a_piece, a_recipe, a_out);
  table.Cell();
  table.Cell();
  table.End();
  AddFromEquippedButton(a_frame, a_recipe, a_out);
  Dim("Every keyword is required. If other keys are listed, at least one must "
      "also match.");
  Dim("magicEffect and effectShader keys consider every enchantment effect.");
  if (!a_piece.diffusePaths.empty()) {
    Dim("a material key matches this piece's texture paths:");
    for (const std::string &path : a_piece.diffusePaths) {
      Dim(path.c_str());
    }
  }
}

void UndoRedoButtons(const Studio::RecipeRow &a_recipe,
                     Studio::Intents &a_out) {
  Disabled(a_recipe.undoDepth == 0, [&]() {
    if (ImGui::Button("Undo") && a_recipe.undoDepth > 0) {
      Studio::Post(a_out, Studio::Undo{a_recipe.id});
    }
  });
  Tooltip(std::format("{} edit(s) to undo (Ctrl+Z)", a_recipe.undoDepth));
  ImGui::SameLine();
  Disabled(a_recipe.redoDepth == 0, [&]() {
    if (ImGui::Button("Redo") && a_recipe.redoDepth > 0) {
      Studio::Post(a_out, Studio::Redo{a_recipe.id});
    }
  });
  Tooltip(std::format("{} edit(s) to redo (Ctrl+Y)", a_recipe.redoDepth));
}

}

std::optional<RecipeKey> DefaultKeyOf(const Studio::PieceRow &a_piece) {
  const Studio::KeyChoice *chosen = nullptr;
  for (const auto &key : a_piece.keys) {
    if (key.key.kind == KeyKind::kArmor) {
      chosen = &key;
      break;
    }
  }
  if (!chosen && !a_piece.keys.empty()) {
    chosen = &a_piece.keys.front();
  }
  if (!chosen) {
    return std::nullopt;
  }
  return RecipeKeyOf(chosen->key, chosen->text);
}

namespace {
enum class KeySource : std::uint8_t { kSelectedArmor, kWornPiece, kTypedKey };

struct WornChoice {
  RecipeKey key;
  Studio::PieceRef piece;
  std::string label;
};

struct NewRecipeDraft {
  KeySource source = KeySource::kSelectedArmor;
  KeyKind kind = KeyKind::kArmor;
  std::optional<WornChoice> worn;
};

struct ChosenKey {
  RecipeKey key;
  std::optional<Studio::PieceRef> piece;
};

constexpr KeyKind kTypedKeyKinds[]{
    KeyKind::kArmor,       KeyKind::kKeyword,      KeyKind::kEnchantment,
    KeyKind::kMagicEffect, KeyKind::kEffectShader, KeyKind::kMaterial};

NewRecipeDraft &Draft() {
  static NewRecipeDraft draft;
  return draft;
}

std::optional<RecipeKey> SelectedArmorKey(const Frame &a_frame) {
  return a_frame.piece ? DefaultKeyOf(*a_frame.piece) : std::nullopt;
}

NewRecipeDraft FreshDraft(const Frame &a_frame) {
  return {.source = SelectedArmorKey(a_frame) ? KeySource::kSelectedArmor
                                              : KeySource::kWornPiece};
}

std::optional<RecipeKey> TypedKey(KeyKind a_kind, std::string_view a_operand) {
  if (a_operand.empty()) {
    return std::nullopt;
  }
  RecipeKey key;
  key.kind = a_kind;
  if (a_kind == KeyKind::kMaterial) {
    key.operand = std::string{a_operand};
  } else {
    key.operand = FormRef::From(a_operand);
  }
  return key;
}

void SourceRadio(const char *a_label, KeySource a_source, bool a_enabled) {
  NewRecipeDraft &draft = Draft();
  Disabled(!a_enabled, [&] {
    if (ImGui::RadioButton(a_label, draft.source == a_source)) {
      draft.source = a_source;
    }
  });
  ImGui::SameLine();
}

void SelectedArmorRow(const Frame &a_frame) {
  const bool available = SelectedArmorKey(a_frame).has_value();
  SourceRadio("Selected armor", KeySource::kSelectedArmor, available);
  ImGui::AlignTextToFramePadding();
  Dim(available ? a_frame.piece->armorName : "no armor is selected");
}

void WornPieceRow(const Frame &a_frame) {
  NewRecipeDraft &draft = Draft();
  SourceRadio("A worn piece", KeySource::kWornPiece, true);
  const std::vector<Studio::WornPieceRow> &worn = a_frame.snapshot->worn;
  NextItemWidth(Studio::Width::Fill());
  const char *preview =
      draft.worn ? draft.worn->label.c_str() : "choose a worn piece";
  if (!ImGui::BeginCombo("##worn-piece", preview)) {
    return;
  }
  if (worn.empty()) {
    Dim("the viewed actor wears no armor");
  }
  for (std::size_t i = 0; i < worn.size(); ++i) {
    const std::optional<RecipeKey> key = Studio::ArmorKeyOf(worn[i]);
    const std::string label = Studio::WornPieceLabel(worn[i]);
    ImGui::PushID(static_cast<int>(i));
    if (key && ImGui::Selectable(label.c_str(), false)) {
      draft.worn = WornChoice{*key, worn[i].piece, label};
      draft.source = KeySource::kWornPiece;
    }
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

std::string_view TypedKeyRow(const Frame &a_frame) {
  NewRecipeDraft &draft = Draft();
  SourceRadio("Any key", KeySource::kTypedKey, true);
  const std::string kindName{KeyKindName(draft.kind)};
  NextItemWidth(
      Studio::Width::Px(ButtonWidth("effectShader") + ImGui::GetFrameHeight()),
      a_frame.scale);
  if (ImGui::BeginCombo("##key-kind", kindName.c_str())) {
    for (const KeyKind choice : kTypedKeyKinds) {
      if (ImGui::Selectable(std::string{KeyKindName(choice)}.c_str(),
                            choice == draft.kind)) {
        draft.kind = choice;
        draft.source = KeySource::kTypedKey;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  const std::string_view operand = LiveTextField(
      "new-key",
      draft.kind == KeyKind::kMaterial ? "texture path or pattern"
                                       : "editor ID or plugin form key",
      Studio::Width::Fill(), a_frame.scale);
  if (!operand.empty() && ImGui::IsItemActive()) {
    draft.source = KeySource::kTypedKey;
  }
  return operand;
}

std::optional<ChosenKey> KeyOfDraft(const Frame &a_frame,
                                    std::string_view a_operand) {
  const NewRecipeDraft &draft = Draft();
  switch (draft.source) {
  case KeySource::kSelectedArmor:
    if (const std::optional<RecipeKey> key = SelectedArmorKey(a_frame)) {
      return ChosenKey{*key, a_frame.piece->ref};
    }
    return std::nullopt;
  case KeySource::kWornPiece:
    if (draft.worn) {
      return ChosenKey{draft.worn->key, draft.worn->piece};
    }
    return std::nullopt;
  case KeySource::kTypedKey:
    if (const std::optional<RecipeKey> key = TypedKey(draft.kind, a_operand)) {
      return ChosenKey{*key, std::nullopt};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

void FormLabel(Table &a_table, std::string_view a_label) {
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
  a_table.Cell();
}

void DrawNewRecipeForm(const Frame &a_frame) {
  const std::string proposed =
      Studio::UniqueName("recipe", a_frame.snapshot->loaded);
  Table table =
      Table::Begin("new-recipe-form",
                   {{"", Studio::Width::Fit()}, {"", Studio::Width::Fill()}},
                   Studio::kFormTable);
  if (!table.Open()) {
    return;
  }
  FormLabel(table, "Name");
  const std::string_view typed = LiveTextField(
      "new-name", proposed.c_str(), Studio::Width::Fill(), a_frame.scale);
  const std::string name = typed.empty() ? proposed : std::string{typed};
  const std::optional<std::string> problem =
      Studio::RecipeNameProblem(name, a_frame.snapshot->loaded);
  if (problem) {
    Problem(*problem);
  }
  FormLabel(table, "Key it to");
  SelectedArmorRow(a_frame);
  WornPieceRow(a_frame);
  const std::string_view operand = TypedKeyRow(a_frame);
  const std::optional<ChosenKey> chosen = KeyOfDraft(a_frame, operand);
  Dim(chosen ? std::format("Keyed by {}", chosen->key.ToString())
             : std::string{"Choose what the recipe applies to."});
  table.End();
  Dim("Creates a pulsing glow over the whole piece. Change its colour in "
      "glowColor and its pulse in glowPulse.");
  RightAligned(ButtonWidth("Create"), [&] {
    Disabled(problem.has_value() || !chosen, [&] {
      if (ImGui::Button("Create") && chosen && !problem) {
        Studio::Post(
            *a_frame.intents,
            Studio::CreateRecipe{name, chosen->key, {}, chosen->piece});
        ImGui::CloseCurrentPopup();
      }
    });
  });
}

void DeleteRecipeButton(const Studio::RecipeRow &a_recipe,
                        Studio::Intents &a_out) {
  const std::string title =
      std::format("Delete recipe?###delete-recipe-{}", a_recipe.id);
  const float side = RowButtonWidth();
  if (ImGui::Button("X##delete-recipe", ImVec2{side, side})) {
    ImGui::OpenPopup(title.c_str());
  }
  Tooltip("delete this recipe");
  ConfirmModal(title.c_str(),
               std::format("Delete '{}'? A recipe in the user folder moves "
                           "to {}.json.deleted beside it, replacing an older "
                           "one; a recipe from another folder is unloaded and "
                           "loads again at the next Reload or start.",
                           a_recipe.id, a_recipe.id),
               "Delete",
               [&] { Studio::Post(a_out, Studio::DeleteRecipe{a_recipe.id}); });
}

void NewRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("New recipe")) {
    Draft() = FreshDraft(a_frame);
    ImGui::OpenPopup("new-recipe");
  }
  ImGui::SetNextWindowSizeConstraints(ImVec2{460.0f * a_frame.scale, 0.0f},
                                      ImVec2{640.0f * a_frame.scale, FLT_MAX});
  if (!ImGui::BeginPopup("new-recipe")) {
    return;
  }
  DrawNewRecipeForm(a_frame);
  ImGui::EndPopup();
}

void RenameRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("Rename", ImVec2{ButtonWidth("Rename"), 0.0f})) {
    ImGui::OpenPopup("rename-recipe");
  }
  if (ImGui::BeginPopup("rename-recipe")) {
    const std::string_view typed = LiveTextField(
        "rename", a_frame.recipe->id.c_str(), Studio::Width::Px(240.0f), 1.0f);
    const bool ready = !typed.empty() && typed != a_frame.recipe->id;
    Disabled(!ready, [&]() {
      if (ImGui::Button("Rename##do") && ready) {
        Studio::Post(
            *a_frame.intents,
            Studio::RenameRecipe{a_frame.recipe->id, std::string{typed}});
        ImGui::CloseCurrentPopup();
      }
    });
    ImGui::EndPopup();
  }
  Tooltip("rename the recipe; its file follows when it is the user's");
}

void DuplicateRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("Duplicate", ImVec2{ButtonWidth("Duplicate"), 0.0f})) {
    ImGui::OpenPopup("duplicate-recipe");
  }
  if (!ImGui::BeginPopup("duplicate-recipe")) {
    return;
  }
  const std::string proposed = a_frame.recipe->id + "-copy";
  const std::string_view typed = LiveTextField(
      "duplicate", proposed.c_str(), Studio::Width::Px(240.0f), a_frame.scale);
  const std::string name = typed.empty() ? proposed : std::string{typed};
  const std::optional<std::string> problem =
      Studio::RecipeNameProblem(name, a_frame.snapshot->loaded);
  if (problem) {
    Problem(*problem);
  }
  Disabled(problem.has_value(), [&]() {
    if (ImGui::Button("Duplicate##do") && !problem) {
      Studio::Post(*a_frame.intents,
                   Studio::DuplicateRecipe{a_frame.recipe->id, name});
      ImGui::CloseCurrentPopup();
    }
  });
  ImGui::EndPopup();
}

void RecipeNameLabel(const Frame &a_frame) {
  const Studio::Snapshot *snapshot = a_frame.snapshot;
  const std::string &id = a_frame.recipe->id;
  const std::optional<std::string> renamed = NameLabel(
      {.key = "recipe-name",
       .name = id,
       .taken =
           [snapshot]() {
             return snapshot ? snapshot->loaded : std::vector<std::string>{};
           },
       .editable = !Studio::IsRenameBlocked(*a_frame.state),
       .size = {Studio::Width::Px(240.0f), a_frame.scale}},
      [&] { Dim(id); });
  if (renamed) {
    Studio::Post(*a_frame.intents, Studio::RenameRecipe{id, *renamed});
  }
}

std::string IsolatedOutputLabel(const Studio::RecipeRow &a_recipe,
                                const Studio::Isolation &a_isolation,
                                std::size_t a_output) {
  const Studio::OutputRow *output =
      FindBy(a_recipe.outputs, a_output, &Studio::OutputRow::index);
  if (!output || a_recipe.id != a_isolation.recipeID) {
    return std::format("output {}", a_output + 1);
  }
  return std::format("{} / {}", SurfaceName(output->surface),
                     SlotName(output->slot));
}

void SoloRecipeButton(const Studio::RecipeRow &a_recipe,
                      const Studio::View &a_view, Studio::Intents &a_out) {
  const bool isolating = a_view.Isolating();
  bool soloed = isolating && a_view.isolation.recipeID == a_recipe.id;
  std::string text = "solo: show only this recipe";
  if (isolating) {
    text = "isolating " + a_view.isolation.recipeID;
    if (a_view.isolation.output.has_value()) {
      text += " " + IsolatedOutputLabel(a_recipe, a_view.isolation,
                                        *a_view.isolation.output);
    }
    if (a_view.isolation.layer.has_value()) {
      text += std::format(" layer {}", *a_view.isolation.layer + 1);
    }
  }
  ImGui::PushID("recipe-solo");
  if (SoloButton(soloed, text)) {
    Studio::Post(a_out, Studio::SoloRecipe{a_recipe.id, soloed});
  }
  ImGui::PopID();
}

void DrawRecipeChoice(const Frame &a_frame) {
  if (a_frame.recipe) {
    const float solo = RowButtonWidth();
    const float avail = ImGui::GetContentRegionAvail().x;
    NextItemWidth(
        Studio::Width::Px((std::max)(120.0f, avail - solo - ItemSpacingX())));
    RecipeCombo(a_frame, "##recipe");
    ImGui::SameLine();
    SoloRecipeButton(*a_frame.recipe, ViewOf(a_frame), *a_frame.intents);
  } else {
    NextItemWidth(Studio::Width::Fill());
    RecipeCombo(a_frame, "##recipe");
  }
}

}

void DrawStudioContext(const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.intents) {
    return;
  }
  const float trailing =
      ButtonWidth("Undo") + ItemSpacingX() + ButtonWidth("Redo") +
      ItemSpacingX() + ButtonWidth("Save") + ItemSpacingX() +
      ButtonWidth("Revert") + ItemSpacingX() + RowButtonWidth();
  const std::function<void()> history = [&]() {
    UndoRedoButtons(*a_frame.recipe, *a_frame.intents);
    ImGui::SameLine();
    DrawRecipeSaveRevert(a_frame);
    ImGui::SameLine();
    DeleteRecipeButton(*a_frame.recipe, *a_frame.intents);
  };
  static_cast<void>(
      Rule(Studio::RuleSpec{.text = "Recipe", .leadingSpace = false},
           a_frame.recipe ? trailing : 0.0f,
           a_frame.recipe ? history : std::function<void()>{},
           [&]() { DrawRecipeRuleStatus(a_frame); }));
  NewRecipeButton(a_frame);
  ImGui::SameLine();
  DrawRecipeChoice(a_frame);
  if (const std::optional<Diagnostic> &refused =
          a_frame.state->creationRefusal) {
    Problem(std::format("not created: {}", refused->message));
  }
}

void DrawRecipeSettings(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.intents || !a_frame.names) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Recipe"},
      ButtonWidth("Rename") + ItemSpacingX() + ButtonWidth("Duplicate"),
      [&]() {
        RenameRecipeButton(a_frame);
        ImGui::SameLine();
        DuplicateRecipeButton(a_frame);
      },
      [&]() { RecipeNameLabel(a_frame); }));
  [[maybe_unused]] const std::optional<std::size_t> detail =
      DrawForm("recipe-header", Studio::RecipeHeaderForm(recipe), a_frame);
  DrawDiagnostics(recipe.problems, recipe.heldBack);
}

void DrawRecipeKeys(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.intents) {
    return;
  }
  DrawKeys(a_frame, a_frame.piece ? *a_frame.piece : Studio::PieceRow{},
           *a_frame.recipe, *a_frame.intents);
}

void DrawOutputHeader(const Studio::OutputRow &a_output,
                      std::span<const Studio::FormField> a_scalars,
                      const Frame &a_frame, std::string_view a_note) {
  const Studio::OutputHeader header = Studio::OutputHeaderForm(a_output);
  const std::string label = std::format(
      "{} / {}", SurfaceName(a_output.surface), SlotName(a_output.slot));
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"}, ButtonWidth("Reset"),
      [&]() {
        const bool canEdit = a_frame.recipe && a_frame.intents;
        if (ImGui::Button("Reset") && canEdit) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::ResetOutput{a_output.index});
        }
      },
      [&]() {
        Dim(label);
        if (!a_note.empty()) {
          ImGui::SameLine();
          Dim(a_note);
        }
      }));
  std::vector<Studio::FormField> settings(a_scalars.begin(), a_scalars.end());
  settings.insert(settings.end(), header.fields.begin(), header.fields.end());
  DrawFormWithSignals("settings", settings, a_frame);
  DrawSelector(header.selector, a_output.index, false, a_frame);
}
}
