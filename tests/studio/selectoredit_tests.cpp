// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"
#include "studio/SelectorEdit.h"
#include "test_support.h"

#include <string>
#include <variant>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] SelectorClause AddonClause(const std::string &a_text) {
  SelectorClause clause;
  clause.kind = SelectorKind::kAddon;
  clause.operand = FormRef::From(a_text);
  return clause;
}

[[nodiscard]] SelectorClause GlobClause(SelectorKind a_kind,
                                        const std::string &a_glob) {
  SelectorClause clause;
  clause.kind = a_kind;
  clause.operand = a_glob;
  return clause;
}

[[nodiscard]] const std::string *GlobOf(const Selector &a_selector,
                                        std::size_t a_index) {
  return a_index < a_selector.anyOf.size()
             ? Get<std::string>(a_selector.anyOf[a_index].operand)
             : nullptr;
}

[[nodiscard]] const FormRef *FormOf(const Selector &a_selector,
                                    std::size_t a_index) {
  return a_index < a_selector.anyOf.size() ? a_selector.anyOf[a_index].Form()
                                           : nullptr;
}

void TestViewRoundTrip() {
  const SelectorView all = SelectorViewOf(Selector{});
  Check(all.matchAll && all.clauses.empty(),
        "SelectorViewOf projects an empty selector as match-all with no rows");

  Selector selector;
  selector.anyOf.push_back(AddonClause("0x12345~Skyrim.esm"));
  selector.anyOf.push_back(GlobClause(SelectorKind::kGeometry, "Body*"));
  selector.anyOf.push_back(GlobClause(SelectorKind::kTexture, "*glow.dds"));
  const SelectorView view = SelectorViewOf(selector);
  Check(!view.matchAll && view.clauses.size() == 3,
        "SelectorViewOf projects one row per clause of a populated selector");
  Check(view.clauses[0].kind == SelectorKind::kAddon &&
            view.clauses[0].isForm &&
            view.clauses[0].value == "0x12345~Skyrim.esm",
        "the addon row is a form row carrying the form-ref text");
  Check(view.clauses[1].kind == SelectorKind::kGeometry &&
            !view.clauses[1].isForm && view.clauses[1].value == "Body*",
        "the geometry row is a glob row carrying the glob");
  Check(view.clauses[2].kind == SelectorKind::kTexture &&
            !view.clauses[2].isForm && view.clauses[2].value == "*glow.dds",
        "the texture row is a glob row carrying the glob");
}

void TestAddAndRemove() {
  const Selector one = SelectorWithClause(Selector{}, SelectorKind::kGeometry);
  Check(one.anyOf.size() == 1 && one.anyOf[0].kind == SelectorKind::kGeometry &&
            Get<std::string>(one.anyOf[0].operand) != nullptr,
        "a default geometry clause appends with a glob operand");
  const Selector two = SelectorWithClause(one, SelectorKind::kAddon);
  Check(two.anyOf.size() == 2 && two.anyOf[1].kind == SelectorKind::kAddon &&
            two.anyOf[1].Form() != nullptr,
        "a default addon clause appends with a form operand");

  const Selector without = SelectorWithoutClause(two, 0);
  Check(without.anyOf.size() == 1 &&
            without.anyOf[0].kind == SelectorKind::kAddon,
        "removing an in-range clause drops exactly that clause");
  Check(SelectorWithoutClause(two, 9) == two,
        "removing an out-of-range clause returns the selector unchanged");
  Check(SelectorWithoutClause(Selector{}, 0) == Selector{},
        "removing from a match-all selector is a no-op");
}

void TestKindChange() {
  Selector selector;
  selector.anyOf.push_back(GlobClause(SelectorKind::kGeometry, "Body*"));

  const Selector texture =
      SelectorWithKind(selector, 0, SelectorKind::kTexture);
  Check(texture.anyOf[0].kind == SelectorKind::kTexture &&
            GlobOf(texture, 0) != nullptr && *GlobOf(texture, 0) == "Body*",
        "a glob-to-glob kind change keeps the glob operand");

  const Selector toAddon = SelectorWithKind(selector, 0, SelectorKind::kAddon);
  Check(toAddon.anyOf[0].kind == SelectorKind::kAddon &&
            toAddon.anyOf[0].Form() != nullptr &&
            toAddon.anyOf[0].Form()->text.empty(),
        "converting a glob clause to addon resets the operand to a form");

  const Selector back = SelectorWithKind(toAddon, 0, SelectorKind::kGeometry);
  Check(back.anyOf[0].kind == SelectorKind::kGeometry &&
            GlobOf(back, 0) != nullptr && GlobOf(back, 0)->empty(),
        "converting an addon clause back to a glob resets the operand to text");

  Check(SelectorWithKind(selector, 9, SelectorKind::kAddon) == selector,
        "an out-of-range kind change returns the selector unchanged");
}

void TestOperandSet() {
  Selector globs;
  globs.anyOf.push_back(GlobClause(SelectorKind::kGeometry, ""));
  const Selector setGlob = SelectorWithOperand(globs, 0, "Hood*");
  Check(GlobOf(setGlob, 0) != nullptr && *GlobOf(setGlob, 0) == "Hood*",
        "setting a geometry operand stores the glob string");

  Selector addon;
  addon.anyOf.push_back(AddonClause(""));
  const Selector setForm = SelectorWithOperand(addon, 0, "0xABC~Dawnguard.esm");
  Check(FormOf(setForm, 0) != nullptr &&
            FormOf(setForm, 0)->text == "0xABC~Dawnguard.esm",
        "setting an addon operand parses the text into a form ref");
  const Selector editorId = SelectorWithOperand(addon, 0, "MyAddonEditorID");
  Check(FormOf(editorId, 0) != nullptr &&
            FormOf(editorId, 0)->text == "MyAddonEditorID",
        "an editor-id addon operand is accepted verbatim");

  const Selector blank = SelectorWithOperand(addon, 0, "   ");
  Check(blank == addon,
        "an unparseable (blank) addon ref leaves the clause unchanged");
  Check(SelectorWithOperand(globs, 9, "x") == globs,
        "an out-of-range operand set returns the selector unchanged");
}
}

int main() {
  TestViewRoundTrip();
  TestAddAndRemove();
  TestKindChange();
  TestOperandSet();
  return test::Finish("studio_selectoredit");
}
