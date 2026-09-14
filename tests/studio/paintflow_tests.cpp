#include "recipe/Expression.h"
#include "studio/Intent.h"
#include "studio/Rows.h"
#include "studio/TermTemplates.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
struct Flow {
  MenuState state;
  Recipe target;
  Recipe paint;
  RecipeRow published;
  Intents frame;
  std::vector<PaintUpdateRequest> work;
  std::optional<PaintUpdateResult> result;

  Flow() {
    target.id = "target";
    target.keys.push_back(RecipeKey{});
    paint = PaintRecipe(target, RecipeKey{}, Surface::kMaterial);
    published.id = std::string{kPaintRecipe};
    published.maskRows.push_back(TextRow{std::string{kScratchMask}, "0", 0});
    Reduce(state, SetMode{Mode::kPaint});
    Reduce(state, BeginPaint{target.id, RecipeKey{}, Surface::kMaterial, 7});
  }

  void Ready() { AcknowledgePaintUpdate(state, PaintUpdateResult{7, 0, {}}); }

  void Offer(const TermKind &a_kind) {
    BuiltTerm built =
        BuildTerm(a_kind, MaskPresets{}, PaintSources(state, published, frame));
    frame.push_back(AddTerm{
        Term{TermOp::kAnd, built.expression, "offer", a_kind}, built.edits});
  }

  void DispatchFrame() {
    for (const Intent &intent : frame) {
      Reduce(state, intent);
    }
    frame.clear();
  }

  void Submit() {
    if (const auto update = PendingPaintUpdate(state)) {
      work.push_back(update->request);
      Reduce(state, *update);
    }
  }

  void Drain(bool a_refuse = false) {
    for (const PaintUpdateRequest &request : work) {
      const auto edits = PreparePaintUpdate(&paint, request);
      std::optional<Diagnostic> problem;
      if (a_refuse) {
        problem =
            MakeDiagnostic(Severity::kError, "paint", "injected edit failure");
      } else if (!edits) {
        problem = edits.error();
      } else {
        problem = Apply(paint, *edits);
      }
      result = PaintUpdateResult{request.sessionID, request.revision, problem};
    }
    work.clear();
  }

  void Publish() {
    if (result) {
      AcknowledgePaintUpdate(state, *result);
    }
  }
};

void DelayedUndo() {
  Flow flow;
  flow.Submit();
  Check(flow.work.empty(), "startup acknowledgment gates preview work");
  flow.Ready();
  Reduce(flow.state, AddTerm{Term{TermOp::kSet, "1", "one", RawTerm{}}});
  flow.Submit();
  Reduce(flow.state, UndoMask{});
  flow.Submit();
  Check(flow.work.size() == 1,
        "one preview is inflight while undo remains dirty");
  flow.Drain();
  Check(flow.paint.FindMask(kScratchMask)->text == "1",
        "first queued preview reaches authoritative recipe");
  flow.Publish();
  Check(flow.state.mask.dirty,
        "old acknowledgment cannot acknowledge a later undo");
  flow.Submit();
  flow.Drain();
  flow.Publish();
  Check(flow.paint.FindMask(kScratchMask)->text == "0" &&
            !flow.state.mask.dirty,
        "undo converges even when desired value equals the unpublished old "
        "snapshot");
  Check(flow.target.masks.empty(), "preview never edits the target");
}

void RapidOffersAndKeep() {
  Flow flow;
  flow.Ready();
  flow.Offer(PartitionTerm{32});
  flow.Offer(PartitionTerm{33});
  flow.DispatchFrame();
  Check(flow.state.mask.terms.size() == 2 &&
            flow.state.mask.terms[0].text != flow.state.mask.terms[1].text,
        "same-frame offers reserve distinct names before dispatch");
  flow.Submit();
  flow.Offer(BoneTerm{{"NPC Spine [Spn0]"}});
  flow.DispatchFrame();
  Reduce(flow.state, SoloTerm{1, true});
  Reduce(flow.state, MuteTerm{2, true});
  PaintCommitRequest keep{9,      flow.target.id,
                          "kept", BuildMask(flow.state.mask.terms),
                          7,      flow.state.paint->sources};
  const auto edits = PreparePaintCommit(&flow.paint, &flow.target, keep);
  Check(edits && !Apply(flow.target, *edits),
        "Keep immediately after offers carries unpublished dependencies");
  Check(flow.target.sources.size() == 3 && flow.target.FindMask("kept") &&
            flow.target.FindMask("kept")->text == keep.expression,
        "Keep preserves every term despite solo, mute, and delayed preview");
  flow.Drain();
  flow.Publish();
  flow.Submit();
  flow.Drain();
  flow.Publish();
  Check(!flow.state.mask.dirty && flow.paint.sources.size() == 3,
        "resending previously applied dependencies is idempotent");
  const auto *partition = Get<BakeSource>(flow.paint.sources.front().kind);
  Check(partition && Get<PartitionBake>(partition->bake) &&
            Get<PartitionBake>(partition->bake)->slot == 32,
        "first partition retains its own source definition");
}

void RefusalAndRetry() {
  Flow flow;
  flow.Ready();
  flow.Offer(PartitionTerm{32});
  flow.DispatchFrame();
  flow.Submit();
  flow.Drain(true);
  flow.Publish();
  Check(flow.state.paint->problem && flow.state.mask.dirty &&
            flow.paint.sources.empty(),
        "failed transaction is visible and adds no partial sources");
  flow.Submit();
  Check(flow.work.empty(),
        "a refused preview waits for explicit retry or another edit");
  flow.state.paint->problem.reset();
  flow.Submit();
  flow.Drain();
  flow.Publish();
  Check(!flow.state.paint->problem && !flow.state.mask.dirty &&
            flow.paint.sources.size() == 1,
        "explicit retry converges after a refused transaction");
  const Recipe before = flow.paint;
  PaintUpdateRequest conflict{
      7,
      99,
      "1",
      {AddSource{flow.paint.sources.front().name,
                 MaterialSource{MaterialChannel::kRoughness}}}};
  Check(!PreparePaintUpdate(&flow.paint, conflict) && flow.paint == before,
        "a conflicting dependency refuses the complete update");
}

void SessionTransitions() {
  Flow flow;
  Check(!AcceptIntent(flow.state,
                      BeginPaint{"target", RecipeKey{}, Surface::kMaterial, 8}),
        "repeated frames cannot start a second session");
  Reduce(flow.state, SetMode{Mode::kCompose});
  const BeginPaint late{"target", RecipeKey{}, Surface::kMaterial, 8};
  Check(!AcceptIntent(flow.state, late),
        "mode switch rejects startup posted by the old frame body");
  Reduce(flow.state, late);
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{7, 0, {}});
  Check(flow.state.paint && flow.state.paint->ready &&
            flow.state.mode == Mode::kCompose,
        "startup acknowledgment completes a suspended session without "
        "reopening it");
  Reduce(flow.state, EndPaint{});
  Reduce(flow.state, SetMode{Mode::kPaint});
  Reduce(flow.state, BeginPaint{"target", RecipeKey{}, Surface::kMaterial, 9});
  AcknowledgePaintUpdate(
      flow.state,
      PaintUpdateResult{
          9, 0, MakeDiagnostic(Severity::kError, "paint", "missing target")});
  Check(flow.state.paint->problem && !flow.state.paint->ready,
        "startup failure is visible rather than stranded in silent waiting");
  Reduce(flow.state, EndPaint{});
  Check(!flow.state.paint && !flow.state.layout.maskEditor &&
            flow.state.selection.recipeID == "target",
        "Discard returns explicitly to Compose with the stable target");
  Reduce(flow.state, SetMode{Mode::kPaint});
  Check(!AcceptIntent(flow.state,
                      BeginPaint{std::string{kPaintRecipe}, RecipeKey{},
                                 Surface::kMaterial, 10}),
        "transient Paint can never become the new target");
}

void NewerEditAfterRefusal() {
  Flow flow;
  flow.Ready();
  Reduce(flow.state, AddTerm{Term{TermOp::kSet, "1", "one", RawTerm{}}});
  flow.Submit();
  Reduce(flow.state, SetTermText{0, "0.5"});
  flow.Drain(true);
  flow.Publish();
  Check(!flow.state.paint->problem && flow.state.mask.dirty,
        "an old refusal cannot block a newer correcting edit");
  flow.Submit();
  flow.Drain();
  flow.Publish();
  Check(!flow.state.mask.dirty &&
            flow.paint.FindMask(kScratchMask)->text == "0.5",
        "newer edit automatically converges after an older failed revision");
}

void KeepLifecycleAndLimits() {
  Flow flow;
  flow.Ready();
  flow.state.paint->keepName[0] = 'x';
  Reduce(flow.state, AddTerm{Term{TermOp::kSet, "1", "one", RawTerm{}}});
  Reduce(flow.state,
         KeepPaint{PaintCommitRequest{20, "target", "kept", "1", 7, {}}});
  Reduce(flow.state, UndoMask{});
  Check(
      flow.state.mask.terms.size() == 1,
      "keyboard undo cannot change the submitted stack while Keep is pending");
  AcknowledgePaintCommit(flow.state, PaintCommitResult{20, {}});
  Check(!flow.state.paint && flow.state.mode == Mode::kCompose &&
            !flow.state.layout.maskEditor,
        "successful Keep returns explicitly to Compose");
  Reduce(flow.state, SetMode{Mode::kPaint});
  Reduce(flow.state, BeginPaint{"target", RecipeKey{}, Surface::kMaterial, 8});
  Check(flow.state.paint->keepName.front() == '\0',
        "new session owns a fresh live Keep name buffer");
  AcknowledgePaintCommit(flow.state, PaintCommitResult{20, {}});
  Check(flow.state.paint && flow.state.paint->sessionID == 8,
        "duplicate Keep result cannot end another session");
  std::vector<Term> maximum(kMaxTerms,
                            Term{TermOp::kAnd, "1", "one", RawTerm{}});
  Check(CheckedBuildMask(maximum).has_value(),
        "64 simple terms are accepted completely");
  Reduce(flow.state, LoadMask{maximum, {}});
  Reduce(flow.state, AddTerm{Term{TermOp::kAnd, "1", "extra", RawTerm{}}});
  Check(flow.state.mask.terms.size() == kMaxTerms && flow.state.paint->problem,
        "65th term leaves stack unchanged and reports refusal");
  const std::string boundary = "1" + std::string(kMaxExpressionLength - 1, ' ');
  Check(CheckedBuildMask(
            std::vector<Term>{Term{TermOp::kSet, boundary, {}, RawTerm{}}})
            .has_value(),
        "single term at the exact expression length limit is accepted");
  Check(!CheckedBuildMask(std::vector<Term>{
            Term{TermOp::kSet, boundary + " ", {}, RawTerm{}}}),
        "one byte over the expression length limit is rejected");
}

void ProjectionAndLoadCancellation() {
  Flow flow;
  RecipeRow old;
  old.id = "target";
  flow.Ready();
  ObservePaintRecipe(flow.state, &old);
  Check(flow.state.paint->ready && !flow.state.paint->projected &&
            !flow.state.paint->problem,
        "startup acknowledgment can precede the projected Paint row without a "
        "false disappearance");
  ObservePaintRecipe(flow.state, &flow.published);
  Check(flow.state.paint->projected && !flow.state.paint->problem,
        "later projection completes startup normally");
  ObservePaintRecipe(flow.state, nullptr);
  Check(!flow.state.paint->problem,
        "a temporary missing projection during actor rebuild does not block "
        "preview work");
  ObservePaintRecipe(flow.state, &flow.published);
  Check(flow.state.paint->projected && !flow.state.paint->problem,
        "projection recovers after a transient empty snapshot");
  flow.Submit();
  Check(flow.state.paint->pendingRevision.has_value(),
        "preview can be pending when a load starts");
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{0, 1, {}, true});
  Check(!flow.state.paint && flow.state.mode == Mode::kCompose,
        "load cancellation ends a session whose queued preview will never "
        "execute");
  Reduce(flow.state, SetMode{Mode::kPaint});
  Check(!AcceptIntent(flow.state, BeginPaint{"target", RecipeKey{},
                                             Surface::kMaterial, 8, 0}),
        "a stale pre-load startup intent is refused by the UI protocol");
  Reduce(flow.state,
         BeginPaint{"target", RecipeKey{}, Surface::kMaterial, 8, 1});
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{0, 1, {}, true});
  Check(flow.state.paint && flow.state.paint->sessionID == 8,
        "repeated load-reset snapshot cannot close a new session");
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{0, 2, {}, true});
  Check(!flow.state.paint,
        "load also cancels startup before engine work was executed");
  Reduce(flow.state, SetMode{Mode::kPaint});
  Reduce(flow.state,
         BeginPaint{"target", RecipeKey{}, Surface::kMaterial, 9, 2});
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{9, 0, {}});
  Reduce(flow.state,
         KeepPaint{PaintCommitRequest{22, "target", "kept", "1", 9, {}}});
  Check(flow.state.paint->pendingCommit.has_value(),
        "Keep is pending before cancellation");
  AcknowledgePaintUpdate(flow.state, PaintUpdateResult{0, 3, {}, true});
  Check(!flow.state.paint,
        "load cancellation clears a Keep request dropped with the game queue");
}

void CommonMaskOperations() {
  Flow flow;
  flow.Ready();
  Reduce(flow.state, AddTerm{Term{TermOp::kSet, "0.8", "a", RawTerm{}}});
  Reduce(flow.state, AddTerm{Term{TermOp::kAnd, "0.25", "b", RawTerm{}}});
  const auto value = [&]() {
    const auto built = CheckedBuildMask(
        flow.state.mask.terms, flow.state.mask.solo, flow.state.mask.muted);
    if (!built || built->empty()) {
      return 0.0f;
    }
    const auto program = Program::Parse(*built);
    if (!program) {
      return -1.0f;
    }
    const Value result = program->Evaluate({});
    const auto *scalar = Get<float>(result);
    return scalar ? *scalar : -1.0f;
  };
  Check(test::Near(value(), 0.2f),
        "AND evaluates overlapping terms as product");
  Reduce(flow.state, SetTermOp{1, TermOp::kOr});
  Check(test::Near(value(), 0.8f), "OR evaluates the union");
  Reduce(flow.state, SetTermOp{1, TermOp::kNot});
  Check(test::Near(value(), 0.6f), "NOT subtracts the second term");
  Reduce(flow.state, MuteTerm{1, true});
  Reduce(flow.state, SoloTerm{1, true});
  Check(test::Near(value(), 0.25f), "Solo overrides mute");
  Reduce(flow.state, MoveTerm{1, 0});
  Check(flow.state.mask.solo == 0 && flow.state.mask.muted.contains(0) &&
            test::Near(value(), 0.25f),
        "reorder preserves solo and mute on the same term");
  Reduce(flow.state, RemoveTerm{0});
  Check(!flow.state.mask.solo && flow.state.mask.muted.empty() &&
            test::Near(value(), 0.8f),
        "removing isolated term clears its indices");
  Reduce(flow.state, MuteTerm{0, true});
  flow.Submit();
  flow.Drain();
  flow.Publish();
  Check(flow.paint.FindMask(kScratchMask)->text == "0",
        "all muted previews zero");
  std::vector<Term> oversized{Term{TermOp::kSet, "1", "a", RawTerm{}},
                              Term{TermOp::kAnd,
                                   std::string(kMaxExpressionLength, '1'), "b",
                                   RawTerm{}}};
  Check(!CheckedBuildMask(oversized),
        "oversized expressions explicitly fail instead of accepting a prefix");
  std::vector<Term> tooMany(kMaxTerms + 1,
                            Term{TermOp::kAnd, "1", "a", RawTerm{}});
  Check(!CheckedBuildMask(tooMany),
        "attempting to build 65 terms is explicitly refused");
}
}

int main() {
  DelayedUndo();
  RapidOffersAndKeep();
  RefusalAndRetry();
  SessionTransitions();
  CommonMaskOperations();
  NewerEditAfterRefusal();
  KeepLifecycleAndLimits();
  ProjectionAndLoadCancellation();
  return test::Finish("studio_paintflow");
}
