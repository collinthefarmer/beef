// GPL-3.0-only with the additional permission in COPYING.md.
#include "menu/PaintPanel.h"
#include "menu/PatternChooser.h"

#include "PCH.h"
#include "engine/RecipeStore.h"
#include "menu/ContextRows.h"
#include "menu/FormDraw.h"
#include "menu/MenuWidgets.h"
#include "menu/Tuning.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"
#include "studio/Edits.h"
#include "studio/FieldCheck.h"
#include "studio/Forms.h"
#include "studio/Intent.h"
#include "studio/Mask.h"
#include "studio/MenuState.h"
#include "studio/Names.h"
#include "studio/PaintSession.h"
#include "studio/Presets.h"
#include "studio/TermTemplates.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
constexpr const char *kTermPayload = "BEEF_TERM";

const std::vector<std::string> kTermOps{
    std::string{Studio::TermOpName(Studio::TermOp::kAnd)},
    std::string{Studio::TermOpName(Studio::TermOp::kOr)},
    std::string{Studio::TermOpName(Studio::TermOp::kNot)}};

void Refuse(std::string_view a_field, const std::string &a_text) {
  logger::warn("{} not applied: '{}' does not parse", a_field, a_text);
}

void RecipeCombo(const Studio::PieceRow &a_piece,
                 const Studio::RecipeRow &a_recipe, const Frame &a_frame) {
  if (!ImGui::BeginCombo("##recipe", RecipeLabel(a_recipe).c_str())) {
    return;
  }
  for (const auto &recipe : a_piece.recipes) {
    const auto label =
        recipe.pinned
            ? RecipeLabel(recipe)
            : std::format("{} ({}, priority {})", recipe.id,
                          recipe.matchedKey.ToString(), recipe.priority);
    if (ImGui::Selectable(label.c_str(), &recipe == &a_recipe)) {
      Studio::Post(*a_frame.intents, Studio::PickRecipe{recipe.id});
    }
  }
  ImGui::EndCombo();
}

[[nodiscard]] Table BeginTermTable() {
  const Studio::Width button = Studio::Width::Px(RowButtonWidth());
  return Table::Begin("terms",
                      {{"#", Studio::Width::Fit()},
                       {"", button},
                       {"", button},
                       {"S", button},
                       {"M", button},
                       {"op", Studio::Width::Fit("and")},
                       {"term", Studio::Width::Fit()},
                       {"detail", Studio::Width::Fill()},
                       {"", button}},
                      Studio::kLayerTable);
}

void CommitTermField(std::size_t a_index, const Studio::TermField &a_field,
                     const std::string &a_text, const Frame &a_frame) {
  const std::optional<Studio::TermKind> changed =
      a_field.apply ? a_field.apply(a_text) : std::nullopt;
  if (!changed) {
    Refuse(a_field.field.name, a_text);
    return;
  }
  const Studio::MaskPresets &presets = LoadedPresets();
  Studio::BuiltTerm built = Studio::BuildTerm(
      *changed, presets,
      Studio::PaintSources(*a_frame.state, *a_frame.recipe, *a_frame.intents));
  Studio::Post(*a_frame.intents,
               Studio::SetTermKind{
                   a_index, *changed, std::move(built.expression),
                   Studio::TermLabelOf(*changed, presets, *a_frame.geometry),
                   std::move(built.edits)});
}

void DrawTermSettings(std::size_t a_index, const Studio::Term &a_term,
                      const Frame &a_frame) {
  const std::vector<Studio::TermField> form =
      Studio::TermForm(a_term.kind, LoadedPresets(), *a_frame.geometry);
  if (form.empty()) {
    return;
  }
  auto table = Table::Begin(
      "term-settings",
      {{"setting", Studio::Width::Fit()}, {"value", Studio::Width::Fill()}},
      Studio::kFormTable);
  if (!table.Open()) {
    return;
  }
  for (const auto &setting : form) {
    ImGui::PushID(setting.field.name.c_str());
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(setting.field.name.c_str());
    table.Cell();
    if (const auto text =
            FieldInput(setting.field, a_frame.scale, *a_frame.names)) {
      CommitTermField(a_index, setting, *text, a_frame);
    }
    ImGui::PopID();
  }
  table.End();
}

void DrawReadPreview(const Studio::PictureRow &a_image, const Frame &a_frame) {
  const float side = a_frame.state->layout.inspectorThumbnail * a_frame.scale;
  Thumbnail(Studio::ThumbnailSpec{a_image.texture, a_image.channel,
                                  a_image.animated, side});
  ImGui::TextUnformatted((Studio::ReferenceText(a_image.name) + " =").c_str());
  ImGui::SameLine();
  ImGui::TextWrapped("%s", a_image.description.c_str());
  if (!a_image.problem.empty()) {
    Warn(a_image.problem);
  }
}

void DrawTermReads(const Studio::Term &a_term, const Frame &a_frame) {
  if (a_term.text.empty()) {
    return;
  }
  const std::expected<Program, std::string> program =
      Program::Parse(a_term.text);
  if (!program || program->References().empty()) {
    return;
  }
  auto table = Table::Begin("term-reads",
                            {{"reads", Studio::Width::Fit()},
                             {"", Studio::Width::Px(ImGui::GetFrameHeight())},
                             {"definition", Studio::Width::Fill()}},
                            Studio::kFormTable);
  if (!table.Open()) {
    return;
  }
  const Studio::GeometryRow &geometry = *a_frame.geometry;
  for (const auto &name : program->References()) {
    const Studio::PictureRow *image = nullptr;
    for (const auto *list : {&geometry.sources, &geometry.masks}) {
      if (const Studio::PictureRow *found = FindByName(*list, name)) {
        image = found;
      }
    }
    ImGui::PushID(name.c_str());
    table.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Studio::ReferenceText(name).c_str());
    table.Cell();
    const auto title =
        std::format("{}###term-read", Studio::ReferenceText(name));
    if (image && DetailButton()) {
      ImGui::OpenPopup(title.c_str());
    }
    table.Cell();
    ImGui::AlignTextToFramePadding();
    if (image) {
      ImGui::TextUnformatted(image->description.c_str());
    } else {
      Warn("not a source or mask of the recipe");
    }
    DetailModal(title.c_str(), [&]() {
      if (image) {
        DrawReadPreview(*image, a_frame);
      }
    });
    ImGui::PopID();
  }
  table.End();
}

void DrawTermDetails(std::size_t a_index, const Studio::Term &a_term,
                     const Frame &a_frame) {
  ImGui::PushID("term-details");
  DrawTermSettings(a_index, a_term, a_frame);
  auto fields = Table::Begin(
      "term-fields",
      {{"field", Studio::Width::Fit()}, {"value", Studio::Width::Fill()}},
      Studio::kFormTable);
  if (fields.Open()) {
    fields.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("op");
    fields.Cell();
    if (a_index == 0) {
      ImGui::AlignTextToFramePadding();
      Dim("set (the first term leads)");
    } else if (const auto chosen = ChoiceCombo(
                   "op", std::string{Studio::TermOpName(a_term.op)}, kTermOps,
                   {Studio::Width::Fit("and"), a_frame.scale})) {
      if (const auto op = Studio::ParseTermOp(*chosen)) {
        Studio::Post(*a_frame.intents, Studio::SetTermOp{a_index, *op});
      }
    }
    fields.Cell();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("expression");
    fields.Cell();
    Badge(Studio::FieldKind::kMask);
    const TextCheck check =
        [&](const std::string &a_text) -> std::optional<std::string> {
      const std::optional<Diagnostic> diagnostic =
          Studio::CheckMaskText(a_text, *a_frame.names);
      return diagnostic ? std::optional<std::string>{ProblemText(diagnostic)}
                        : std::nullopt;
    };
    if (const auto edited =
            TextField("text", a_term.text,
                      {Studio::Width::Fill(), a_frame.scale}, check)) {
      Studio::Post(*a_frame.intents, Studio::SetTermText{a_index, *edited});
    }
    fields.End();
  }
  DrawTermReads(a_term, a_frame);
  ImGui::PopID();
}

void DrawTermControls(Table &a_table, std::size_t a_index,
                      const Frame &a_frame) {
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%zu", a_index);
  a_table.Cell();
  if (RemoveButton(0)) {
    Studio::Post(*a_frame.intents, Studio::RemoveTerm{a_index});
  }
  a_table.Cell();
  if (DragHandle(kTermPayload, a_index, "term")) {
    Studio::Post(*a_frame.intents, Studio::PickTerm{a_index});
  }
  if (const auto move = DropTarget(kTermPayload, a_index)) {
    Studio::Post(*a_frame.intents, Studio::MoveTerm{move->from, move->to});
  }
  a_table.Cell();
  bool solo = a_frame.state->mask.solo == a_index;
  if (SoloButton(solo)) {
    Studio::Post(*a_frame.intents, Studio::SoloTerm{a_index, solo});
  }
  a_table.Cell();
  bool mute = a_frame.state->mask.muted.contains(a_index);
  if (MuteButton(mute)) {
    Studio::Post(*a_frame.intents, Studio::MuteTerm{a_index, mute});
  }
}

void DrawTermRow(Table &a_table, std::size_t a_index,
                 std::span<const Studio::TermOffer> a_offers,
                 const Frame &a_frame) {
  const Studio::MaskStack &mask = a_frame.state->mask;
  if (a_index >= mask.terms.size()) {
    return;
  }
  const Studio::Term &term = mask.terms[a_index];
  ImGui::PushID(static_cast<int>(a_index));
  DrawTermControls(a_table, a_index, a_frame);
  a_table.Cell();
  if (a_index == 0) {
    ImGui::AlignTextToFramePadding();
    Dim("set");
  } else if (const auto chosen =
                 ChoiceCombo("op", std::string{Studio::TermOpName(term.op)},
                             kTermOps, {Studio::Width::Fit("and"), 1.0f})) {
    if (const auto op = Studio::ParseTermOp(*chosen)) {
      Studio::Post(*a_frame.intents, Studio::SetTermOp{a_index, *op});
    }
  }
  a_table.Cell();
  const bool raw = std::holds_alternative<Studio::RawTerm>(term.kind);
  const std::string label =
      raw && term.text.empty() ? std::string{"(empty)"} : term.label;
  if (ImGui::Selectable(label.c_str(), mask.selected == a_index)) {
    Studio::Post(*a_frame.intents, Studio::PickTerm{a_index});
  }
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  Dim(Studio::TermDetailOf(term, a_offers));
  a_table.Cell();
  const auto title =
      std::format("term {}: {}###term-details", a_index, term.label);
  if (DetailButton()) {
    ImGui::OpenPopup(title.c_str());
  }
  DetailModal(title.c_str(),
              [&]() { DrawTermDetails(a_index, term, a_frame); });
  ImGui::PopID();
}

}

namespace {
void DrawPaintSurface(const Frame &a_frame) {
  const Studio::MenuState &state = *a_frame.state;
  if (!state.paint) {
    return;
  }
  const float comboWidth = FitWidth("material");
  ImGui::AlignTextToFramePadding();
  Dim("preview on");
  ImGui::SameLine();
  if (const auto chosen = ChoiceCombo(
          "surface", std::string{SurfaceName(state.paint->surface)},
          WordsOf(kSurfaces), {Studio::Width::Px(comboWidth), 1.0f})) {
    if (const auto surface = ParseSurface(*chosen)) {
      Studio::Post(*a_frame.intents, Studio::SetPaintSurface{*surface});
    }
  }
}
}

void DrawPaintHead(const Frame &a_frame) {
  if (!a_frame.piece || !a_frame.recipe) {
    return;
  }
  const Studio::MenuState &state = *a_frame.state;
  if (state.paint) {
    HeldLabel(state.paint->recipeID.c_str());
    ImGui::SameLine();
    const float labelWidth = TextWidth("preview on");
    const float comboWidth = FitWidth("material");
    RightAligned(labelWidth + ItemSpacingX() + comboWidth,
                 [&]() { DrawPaintSurface(a_frame); });
  } else {
    NextItemWidth(Studio::Width::Fit(RecipeLabel(*a_frame.recipe)));
    RecipeCombo(*a_frame.piece, *a_frame.recipe, a_frame);
  }
}

namespace {
void KeepMaskPopup(const Frame &a_frame) {
  Studio::MenuState &state = *a_frame.state;
  const Studio::MaskStack &mask = state.mask;
  const bool painting = state.paint.has_value();
  if (ImGui::BeginPopup("keep-mask")) {
    const std::string proposed =
        Studio::ProposedMaskName(mask.terms, mask.editing);
    if (painting) {
      NextItemWidth(Studio::Width::Px(200.0f));
      ImGui::InputTextWithHint("name", proposed.c_str(),
                               state.paint->keepName.data(),
                               state.paint->keepName.size());
    }
    const std::string name = painting && state.paint->keepName.front() != '\0'
                                 ? std::string{state.paint->keepName.data()}
                                 : proposed;
    const auto expression = Studio::CheckedBuildMask(mask.terms);
    const bool ready = painting && state.paint->ready && expression &&
                       !state.paint->pendingCommit && IsName(name) &&
                       name != std::string{Studio::kScratchMask};
    if (painting && !state.paint->assignment && !mask.editing.empty()) {
      Dim("Keeping this name updates the shared mask and all of its "
          "consumers.");
    }
    Disabled(!ready, [&]() {
      const std::string label = painting && state.paint->assignment
                                    ? std::format("Keep and assign as {}", name)
                                    : std::format("Keep as {}", name);
      if (ImGui::Button(label.c_str()) && ready) {
        Studio::Post(*a_frame.intents,
                     Studio::KeepPaint{Studio::PaintCommitRequest{
                         state.nextPaintCommitID++, state.paint->recipeID, name,
                         *expression, state.paint->sessionID,
                         Studio::PaintDependencies(state),
                         state.paint->assignment, state.mask.editing}});
        ImGui::CloseCurrentPopup();
      }
    });
    ImGui::EndPopup();
  }
}

void DiscardMaskDraft(const Frame &a_frame) {
  const Studio::MenuState &state = *a_frame.state;
  if (state.paint && state.paint->createdMask) {
    std::vector<Studio::RecipeEdit> edits;
    edits.emplace_back(Studio::RemoveMask{*state.paint->createdMask, true});
    Studio::Post(*a_frame.intents,
                 Studio::EditRecipe{state.paint->recipeID, std::move(edits)});
  }
  Studio::Post(*a_frame.intents, Studio::EndPaint{});
}
}

void DrawMaskRule(std::string_view a_title, const Frame &a_frame) {
  const Studio::MenuState &state = *a_frame.state;
  const Studio::MaskStack &mask = state.mask;
  const bool painting = state.paint.has_value();
  const auto expression = Studio::CheckedBuildMask(mask.terms);
  const bool canKeep = painting && state.paint->ready &&
                       !state.paint->pendingCommit && expression &&
                       !expression->empty();

  const Studio::RuleButton buttons[]{
      {Studio::RuleAction::kUndo,
       {},
       Studio::Width::Fit(),
       state.maskHistory.UndoDepth() > 0},
      {Studio::RuleAction::kRedo,
       {},
       Studio::Width::Fit(),
       state.maskHistory.RedoDepth() > 0},
      {Studio::RuleAction::kClear,
       {},
       Studio::Width::Fit(),
       !mask.terms.empty()},
      {Studio::RuleAction::kAdd, "Keep", Studio::Width::Fit(), canKeep},
  };
  const Studio::RuleSpec spec{a_title, buttons};
  const Studio::RuleClick click =
      Rule(spec, 0.0f, {}, [&]() {
        std::string errors;
        if (!expression) {
          errors = expression.error().message;
        }
        if (state.paint && state.paint->problem) {
          if (!errors.empty()) {
            errors += '\n';
          }
          errors += state.paint->problem->message;
        }
        if (!errors.empty()) {
          ProblemBadge("!!!");
          Tooltip(errors);
        }
        if (state.paint && state.paint->problem && state.paint->ready) {
          ImGui::SameLine();
          if (ImGui::Button("Retry preview")) {
            a_frame.state->paint->problem.reset();
          }
        }
      }).click;
  if (click.clicked && click.index < std::size(buttons)) {
    switch (buttons[click.index].action) {
    case Studio::RuleAction::kUndo:
      Studio::Post(*a_frame.intents, Studio::UndoMask{});
      break;
    case Studio::RuleAction::kRedo:
      Studio::Post(*a_frame.intents, Studio::RedoMask{});
      break;
    case Studio::RuleAction::kClear:
      Studio::Post(*a_frame.intents, Studio::ClearMask{});
      break;
    case Studio::RuleAction::kAdd:
      ImGui::OpenPopup("keep-mask");
      break;
    default:
      break;
    }
  }

  KeepMaskPopup(a_frame);
}

void DrawMaskStack(const Frame &a_frame) {
  if (!a_frame.piece || !a_frame.recipe || !a_frame.geometry) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const Studio::GeometryRow &geometry = *a_frame.geometry;
  const Studio::MaskStack &mask = a_frame.state->mask;
  const std::vector<Studio::TermOffer> offers =
      Studio::OffersOfRecipe(LoadedPresets(), recipe, mask.editing);

  const ImGuiMCP::ImVec2 avail = ImGui::GetContentRegionAvail();
  const float termsCap =
      (std::min)(avail.y * 0.45f, ImGui::GetFrameHeightWithSpacing() * 7.0f);
  ImGui::SetNextWindowSizeConstraints(ImGuiMCP::ImVec2{0.0f, 0.0f},
                                      ImGuiMCP::ImVec2{avail.x, termsCap});
  if (ImGui::BeginChild("mask-terms", ImGuiMCP::ImVec2{0.0f, 0.0f},
                        ImGuiMCP::ImGuiChildFlags_AutoResizeY, 0)) {
    auto table = BeginTermTable();
    if (table.Open()) {
      for (std::size_t i = 0; i < mask.terms.size(); ++i) {
        DrawTermRow(table, i, offers, a_frame);
      }
      table.End();
    }
    if (mask.terms.empty()) {
      Dim("no selection yet: choose a term below");
    }
  }
  ImGui::EndChild();

  HelpMarker(
      "A mask is terms combined in order: the first sets it, each next one "
      "is "
      "and (product), or (max) or not (times the complement). Drag the :: "
      "grip to reorder; S shows one term alone, M leaves one out; ... opens "
      "a "
      "term's settings; Keep writes every term.");

  const char *hint = "filter by kind, name or measurement";
  const Studio::RuleSpec termsSpec{"Terms", {}};
  const RuleFilter rule = RuleWithFilter(
      termsSpec, {"offer-filter", hint, FitWidth(hint), a_frame.scale});
  if (offers.empty()) {
    Dim(geometry.meshRead ? "nothing to offer on this geometry"
                          : "reading the mesh");
  }
  if (ImGui::BeginChild("mask-offers", ImGuiMCP::ImVec2{0.0f, 0.0f}, 0, 0)) {
    DrawPatternChooser(offers, rule.filter,
                       mask.terms.size() >= Studio::kMaxTerms, a_frame);
  }
  ImGui::EndChild();
}

void RebuildScratch(const Frame &a_frame) {
  Studio::MenuState *state = a_frame.state;
  const Studio::RecipeRow *recipe = a_frame.recipe;
  if (!state || !recipe || !a_frame.intents) {
    return;
  }
  if (const auto update = Studio::PendingPaintUpdate(*state)) {
    Studio::Post(*a_frame.intents, *update);
  }
}

void EditMaskAsTerms(const Studio::TextRow &a_mask, const Frame &a_frame,
                     bool a_created) {
  if (!a_frame.recipe || !a_frame.piece || a_frame.state->paint) {
    return;
  }
  const auto key = DefaultKeyOf(*a_frame.piece);
  if (!key) {
    return;
  }
  const std::string label = Studio::TermLabel(
      a_mask.text, LoadedPresets(), Studio::SourceCatalogOf(*a_frame.recipe));
  Studio::Post(*a_frame.intents,
               Studio::LoadMask{{Studio::Term{Studio::TermOp::kSet, a_mask.text,
                                              label, Studio::RawTerm{}}},
                                a_mask.name});
  Studio::Post(*a_frame.intents, Studio::SetMode{Studio::Mode::kPaint});
  std::optional<Studio::PaintAssignment> assignment;
  if (const auto *layer =
          Get<Studio::LayerSubject>(SelectionOf(a_frame).subject)) {
    assignment = Studio::PaintAssignment{layer->output, layer->layer,
                                         a_frame.recipe->documentRevision};
  }
  std::optional<std::string> createdMask;
  if (a_created) {
    createdMask = a_mask.name;
  }
  Studio::Post(*a_frame.intents,
               Studio::BeginPaint{a_frame.recipe->id, *key, Surface::kMaterial,
                                  a_frame.state->nextPaintSessionID++,
                                  a_frame.state->lastPaintReset, assignment,
                                  createdMask});
  if (!a_mask.name.empty()) {
    a_frame.state->pendingSelection = Studio::MaskSubject{a_mask.name};
  }
}

void DrawPaintDraftBar(const Frame &a_frame) {
  Studio::MenuState &state = *a_frame.state;
  if (!state.paint) {
    return;
  }
  const bool active = Studio::MaskTaskActive(state);
  const std::string &editing = state.mask.editing;
  std::string label =
      std::format("Mask draft: {}", editing.empty() ? "unsaved mask" : editing);
  if (state.paint->assignmentInvalid) {
    label += " (destination changed)";
  }
  const bool pending = state.paint->pendingCommit.has_value();
  const bool hasTerms = !state.mask.terms.empty();
  const float trailingWidth =
      active ? ButtonWidth("Discard")
             : ButtonWidth("Resume") + ItemSpacingX() + ButtonWidth("Discard");
  Banner(label, trailingWidth, [&] {
    if (!active) {
      if (ImGui::Button("Resume") && !editing.empty()) {
        state.pendingSelection = Studio::MaskSubject{editing};
      }
      ImGui::SameLine();
    }
    Disabled(pending, [&] {
      if (ImGui::Button("Discard")) {
        if (hasTerms) {
          ImGui::OpenPopup("Discard mask draft?###discard-mask");
        } else {
          DiscardMaskDraft(a_frame);
        }
      }
    });
  });
  ConfirmModal("Discard mask draft?###discard-mask",
               std::format("The {} term(s) in this draft will be lost.",
                           state.mask.terms.size()),
               "Discard", [&] { DiscardMaskDraft(a_frame); });
}

void DrawMaskTask(const Frame &a_frame) {
  Studio::MenuState &state = *a_frame.state;
  if (!state.paint) {
    return;
  }
  DrawPaintHead(a_frame);
  std::string title = state.mask.editing.empty() ? std::string{"New mask draft"}
                                                 : state.mask.editing;
  if (state.paint->assignment) {
    title += std::format(" -> output {} / layer {}",
                         state.paint->assignment->output + 1,
                         state.paint->assignment->layer + 1);
  }
  DrawMaskRule(title, a_frame);
  if (!state.paint->ready) {
    PlaceholderText("Waiting for the mask preview.");
    return;
  }
  const Studio::PieceRow *piece =
      FindBy(a_frame.snapshot->pieces, state.paint->origin.piece,
             &Studio::PieceRow::ref);
  if (!piece) {
    PlaceholderText(
        "The original armor is unavailable. The draft is retained.");
    return;
  }
  const Studio::RecipeRow *paint =
      FindById(piece->recipes, Studio::kPaintRecipe);
  if (!paint) {
    PlaceholderText("Waiting for the mask preview geometry.");
    return;
  }
  Studio::ObservePaintRecipe(state, paint);
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(paint, state.paint->origin);
  if (!geometry) {
    PlaceholderText("No geometry is available for this mask preview.");
    return;
  }
  for (const Studio::GeometryRow &row : paint->geometries) {
    if (!state.paint->readGeometries.contains(row.name)) {
      Studio::Post(*a_frame.intents,
                   Studio::ReadMesh{piece->ref.actorID, row.name});
    }
  }
  const Studio::Names names = Studio::NamesOf(*paint, *geometry);
  Frame preview = a_frame;
  preview.piece = piece;
  preview.recipe = paint;
  preview.geometry = geometry;
  preview.names = &names;
  DrawMaskStack(preview);
}

namespace {
std::optional<Frame> PaintPreviewFrame(const Frame &a_frame,
                                       Studio::Names &a_names) {
  Studio::MenuState &state = *a_frame.state;
  if (!state.paint || !a_frame.snapshot) {
    return std::nullopt;
  }
  const Studio::PieceRow *piece =
      FindBy(a_frame.snapshot->pieces, state.paint->origin.piece,
             &Studio::PieceRow::ref);
  if (!piece) {
    return std::nullopt;
  }
  const Studio::RecipeRow *paint =
      FindById(piece->recipes, Studio::kPaintRecipe);
  if (!paint) {
    return std::nullopt;
  }
  const Studio::GeometryRow *geometry =
      Studio::SelectedGeometry(paint, state.paint->origin);
  if (!geometry) {
    return std::nullopt;
  }
  a_names = Studio::NamesOf(*paint, *geometry);
  Frame preview = a_frame;
  preview.piece = piece;
  preview.recipe = paint;
  preview.geometry = geometry;
  preview.names = &a_names;
  return preview;
}

void DrawTermParameter(std::size_t a_index, const Studio::TermField &a_field,
                       const Frame &a_preview, Table &a_table) {
  ImGui::PushID(a_field.field.name.c_str());
  a_table.Cell();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(a_field.field.name.c_str());
  a_table.Cell();
  const bool tunable = a_field.field.kind == Studio::FieldKind::kScalar &&
                       (a_field.field.range || a_field.field.workingRange);
  if (tunable) {
    DrawTuning(a_field.field, a_preview,
               TuningSink{[a_index, &a_field, &a_preview](float a_value) {
                 const std::string text =
                     a_field.field.integral
                         ? std::to_string(
                               static_cast<long long>(std::llround(a_value)))
                         : ParamText(Param{a_value});
                 CommitTermField(a_index, a_field, text, a_preview);
               }});
  } else if (const auto text =
                 FieldInput(a_field.field, a_preview.scale, *a_preview.names)) {
    CommitTermField(a_index, a_field, *text, a_preview);
  }
  ImGui::PopID();
}
}

void DrawTermTuningPane(const Frame &a_frame) {
  Studio::MenuState &state = *a_frame.state;
  if (state.mode != Studio::Mode::kPaint) {
    return;
  }
  const Studio::MaskStack &mask = state.mask;
  if (!mask.selected || *mask.selected >= mask.terms.size()) {
    return;
  }
  Studio::Names names;
  const std::optional<Frame> preview = PaintPreviewFrame(a_frame, names);
  if (!preview) {
    return;
  }
  const std::size_t index = *mask.selected;
  const Studio::Term &term = mask.terms[index];
  const std::vector<Studio::TermField> form =
      Studio::TermForm(term.kind, LoadedPresets(), *preview->geometry);
  Dim(std::format("Tuning term {}: {}", index, term.label));
  if (form.empty()) {
    Dim("This term has no adjustable settings.");
    return;
  }
  const FieldScope maskScope("mask");
  const FieldScope termScope(std::to_string(index));
  auto table = Table::Begin(
      "term-tune",
      {{"setting", Studio::Width::Fit()}, {"value", Studio::Width::Fill()}},
      Studio::kFormTable);
  if (!table.Open()) {
    return;
  }
  for (const Studio::TermField &field : form) {
    DrawTermParameter(index, field, *preview, table);
  }
  table.End();
}
}
