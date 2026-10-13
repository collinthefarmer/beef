// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/NameEdit.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
const std::vector<std::string> kTaken{"drive", "pattern", "tone"};
const NameRules kRules{.current = "drive", .taken = kTaken};
constexpr FieldKey kLabel = 7;

NameDraft Focused(std::string a_text) {
  return NameDraft{
      .label = kLabel, .text = std::move(a_text), .awaitingFocus = false};
}

void Begins() {
  const NameDraft draft = BeginNameDraft(kLabel, "drive");
  Check(draft.label == kLabel && draft.text == "drive" && draft.awaitingFocus,
        "a draft starts with the name and waits for focus");
  const std::optional<NameDraft> open = draft;
  Check(IsNaming(open, kLabel) && !IsNaming(open, kLabel + 1) &&
            !IsNaming(std::nullopt, kLabel),
        "only the label that began the draft is naming");
}

void Problems() {
  Check(!NameProblem("drive", kRules), "the current name is not a problem");
  Check(!NameProblem("drive_2", kRules), "a free valid name is not a problem");
  Check(NameProblem("", kRules).has_value(), "an empty name is a problem");
  Check(NameProblem("2drive", kRules).has_value(),
        "a name starting with a digit is a problem");
  Check(NameProblem("dr ive", kRules).has_value(),
        "a name with a space is a problem");
  Check(NameProblem("pattern", kRules) ==
            std::optional<std::string>{"That name is already in use."},
        "a taken name is a problem");
}

void Commits() {
  const NameStep step =
      StepNameDraft(Focused("glow"), {"glow", true, NameEvent::kEnter}, kRules);
  const auto *commit = Get<CommitName>(step);
  Check(commit && commit->to == "glow", "Enter commits a valid new name");

  const NameStep same = StepNameDraft(
      Focused("drive"), {"drive", true, NameEvent::kEnter}, kRules);
  Check(Is<CancelNaming>(same), "Enter on the unchanged name ends the draft");
}

void RefusesInvalid() {
  const NameStep step = StepNameDraft(
      Focused("pattern"), {"pattern", false, NameEvent::kEnter}, kRules);
  const auto *keep = Get<KeepNaming>(step);
  Check(keep && keep->problem && keep->draft.text == "pattern" &&
            keep->draft.awaitingFocus,
        "Enter on an invalid name keeps the draft, shows the problem and "
        "asks for focus again");

  const NameStep typing = StepNameDraft(
      Focused("drive"), {"9lives", true, NameEvent::kNone}, kRules);
  const auto *live = Get<KeepNaming>(typing);
  Check(live && live->problem && live->draft.text == "9lives" &&
            !live->draft.awaitingFocus,
        "typing an invalid name shows the problem without committing");
}

void Cancels() {
  Check(Is<CancelNaming>(StepNameDraft(
            Focused("glow"), {"glow", false, NameEvent::kDeactivated}, kRules)),
        "losing focus or Escape cancels the draft");
  Check(Is<CancelNaming>(
            StepNameDraft(BeginNameDraft(kLabel, "drive"),
                          {"drive", false, NameEvent::kClickedAway}, kRules)),
        "a click away cancels even before the field takes focus");
  const NameStep waiting =
      StepNameDraft(BeginNameDraft(kLabel, "drive"),
                    {"drive", false, NameEvent::kDeactivated}, kRules);
  Check(Is<KeepNaming>(waiting),
        "a deactivation before the field took focus keeps the draft");
  const NameStep focused =
      StepNameDraft(BeginNameDraft(kLabel, "drive"),
                    {"drive", true, NameEvent::kNone}, kRules);
  const auto *keep = Get<KeepNaming>(focused);
  Check(keep && !keep->draft.awaitingFocus,
        "the draft stops waiting once the field is active");
}
}

int main() {
  Begins();
  Problems();
  Commits();
  RefusesInvalid();
  Cancels();
  return test::Finish("studio_nameedit");
}
