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
#include "studio/Names.h"
#include "studio/Selection.h"
#include "studio/SelectorEdit.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;

namespace BetterEnchantmentEffects::Menu {
namespace {

[[nodiscard]] std::string RecipeLabel(const Studio::RecipeRow &a_recipe) {
  return a_recipe.pinned ? std::format("{} (pinned here)", a_recipe.id)
                         : a_recipe.id;
}

void RecipeCombo(const Frame &a_frame, const char *a_label) {
  const std::string preview = a_frame.recipe ? RecipeLabel(*a_frame.recipe)
                                             : SelectionOf(a_frame).recipeID;
  if (!ImGui::BeginCombo(a_label,
                         preview.empty() ? "Choose recipe" : preview.c_str())) {
    return;
  }
  for (const std::string &id : a_frame.snapshot->loaded) {
    const bool selected = id == SelectionOf(a_frame).recipeID;
    if (ImGui::Selectable(id.c_str(), selected)) {
      Studio::Post(*a_frame.intents, Studio::PickRecipe{id, true});
    }
  }
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

void TextOrDash(std::string_view a_text) {
  ImGui::AlignTextToFramePadding();
  if (a_text.empty()) {
    Dim("—");
  } else {
    ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
  }
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

void DrawKeys(const Frame &a_frame, const Studio::PieceRow &a_piece,
              const Studio::RecipeRow &a_recipe, Studio::Intents &a_out) {
  if (!Rule(Studio::RuleSpec{
                .text = "Keys", .collapsible = true, .leadingSpace = false})
           .open) {
    return;
  }
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
void CreateForSelectedArmor(const Frame &a_frame, const std::string &a_name,
                            bool a_nameValid) {
  if (!a_frame.piece) {
    return;
  }
  const auto key = DefaultKeyOf(*a_frame.piece);
  if (!key) {
    return;
  }
  Disabled(!a_nameValid, [&] {
    if (ImGui::Button("Create for selected armor")) {
      Studio::Post(*a_frame.intents, Studio::CreateRecipe{a_name, *key, {}});
      ImGui::CloseCurrentPopup();
    }
  });
}

void NewRecipeButton(const Frame &a_frame) {
  if (ImGui::Button("New recipe")) {
    ImGui::OpenPopup("new-recipe");
  }
  if (!ImGui::BeginPopup("new-recipe")) {
    return;
  }
  static KeyKind kind = KeyKind::kArmor;
  const std::string proposed =
      Studio::UniqueName("recipe", a_frame.snapshot->loaded);
  const std::string_view typed = LiveTextField(
      "new-name", proposed.c_str(), Studio::Width::Px(240.0f), a_frame.scale);
  const std::string name = typed.empty() ? proposed : std::string{typed};
  const KeyKind kinds[]{KeyKind::kArmor,        KeyKind::kKeyword,
                        KeyKind::kEnchantment,  KeyKind::kMagicEffect,
                        KeyKind::kEffectShader, KeyKind::kMaterial};
  if (ImGui::BeginCombo("Key type", std::string{KeyKindName(kind)}.c_str())) {
    for (const KeyKind choice : kinds) {
      if (ImGui::Selectable(std::string{KeyKindName(choice)}.c_str(),
                            choice == kind)) {
        kind = choice;
      }
    }
    ImGui::EndCombo();
  }
  const std::string_view operand = LiveTextField(
      "new-key",
      kind == KeyKind::kMaterial ? "texture path or pattern"
                                 : "form editor ID or plugin form key",
      Studio::Width::Px(320.0f), a_frame.scale);
  const bool nameValid =
      IsName(name) && !std::ranges::contains(a_frame.snapshot->loaded, name);
  const bool ready = nameValid && !operand.empty();
  Dim("Creates an empty document with the chosen key. Add outputs to make an "
      "effect.");
  Disabled(!ready, [&] {
    if (ImGui::Button("Create document")) {
      RecipeKey key;
      key.kind = kind;
      if (kind == KeyKind::kMaterial) {
        key.operand = std::string{operand};
      } else {
        key.operand = FormRef::From(operand);
      }
      Studio::Post(*a_frame.intents, Studio::CreateRecipe{name, key, {}});
      ImGui::CloseCurrentPopup();
    }
  });
  CreateForSelectedArmor(a_frame, name, nameValid);
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
    ImGui::SameLine();
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

void SoloRecipeButton(const Studio::RecipeRow &a_recipe,
                      const Studio::View &a_view, Studio::Intents &a_out) {
  bool isolating = a_view.Isolating();
  std::string text = "solo: show only this recipe";
  if (isolating) {
    text = "isolating " + a_view.isolation.recipeID;
    if (a_view.isolation.output.has_value()) {
      text += std::format(" output {}", *a_view.isolation.output);
    }
    if (a_view.isolation.layer.has_value()) {
      text += std::format(" layer {}", *a_view.isolation.layer);
    }
  }
  if (SoloButton(isolating, text)) {
    Studio::Post(a_out, Studio::SoloRecipe{a_recipe.id, isolating});
  }
}

}

void DrawStudioContext(const Frame &a_frame) {
  if (!a_frame.snapshot || !a_frame.intents) {
    return;
  }
  float trailing = 0.0f;
  if (a_frame.recipe) {
    trailing = ButtonWidth("Undo") + ItemSpacingX() + ButtonWidth("Redo") +
               ItemSpacingX() + ButtonWidth("Save") + ItemSpacingX() +
               ButtonWidth("Revert");
  }
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Recipe", .leadingSpace = false}, trailing,
      [&]() {
        if (!a_frame.recipe) {
          return;
        }
        UndoRedoButtons(*a_frame.recipe, *a_frame.intents);
        ImGui::SameLine();
        DrawRecipeSaveRevert(a_frame);
      },
      [&]() { DrawRecipeRuleStatus(a_frame); }));
  NewRecipeButton(a_frame);
  ImGui::SameLine();
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

void DrawRecipeSettings(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.intents || !a_frame.names) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Recipe"}, ButtonWidth("Rename"),
      [&]() { RenameRecipeButton(a_frame); }, [&]() { Dim(recipe.id); }));
  [[maybe_unused]] const std::optional<std::size_t> detail =
      DrawForm("recipe-header", Studio::RecipeHeaderForm(recipe), a_frame);
  DrawKeys(a_frame, a_frame.piece ? *a_frame.piece : Studio::PieceRow{}, recipe,
           *a_frame.intents);
  DrawDiagnostics(recipe.problems, recipe.heldBack);
}

void DrawOutputHeader(const Studio::OutputRow &a_output,
                      std::span<const Studio::FormField> a_scalars,
                      const Frame &a_frame, std::string_view a_note) {
  const Studio::OutputHeader header = Studio::OutputHeaderForm(
      a_output.index, a_output.replace, a_output.selection);
  const std::string label = std::format(
      "{} / {}", SurfaceName(a_output.surface), SlotName(a_output.slot));
  static_cast<void>(Rule(
      Studio::RuleSpec{.text = "Inspector"},
      ButtonWidth("Reset") + ItemSpacingX() + RowButtonWidth(),
      [&]() {
        const bool canEdit = a_frame.recipe && a_frame.intents;
        if (ImGui::Button("Reset") && canEdit) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::ResetOutput{a_output.index});
        }
        ImGui::SameLine();
        if (canEdit && RemoveButton(0)) {
          Studio::Post(*a_frame.intents, a_frame.recipe->id,
                       Studio::RemoveOutput{a_output.index});
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
