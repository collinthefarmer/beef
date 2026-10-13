// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/WornPieces.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] FormKey Form(std::uint32_t a_id) {
  return FormKey{"Skyrim.esm", a_id};
}

[[nodiscard]] KeyChoice Choice(KeyKind a_kind, std::uint32_t a_id,
                               std::string a_text) {
  return KeyChoice{PieceKey{a_kind, Form(a_id)}, std::move(a_text)};
}

[[nodiscard]] WornPieceRow Piece(std::uint32_t a_armor, std::string a_name,
                                 std::vector<KeyChoice> a_keys = {}) {
  WornPieceRow row;
  row.armor = Form(a_armor);
  row.armorName = std::move(a_name);
  row.keys = std::move(a_keys);
  return row;
}

[[nodiscard]] RecipeKey Keyword(std::uint32_t a_id, std::string a_text) {
  return RecipeKeyOf(PieceKey{KeyKind::kKeyword, Form(a_id)}, a_text);
}

[[nodiscard]] std::vector<std::string>
TextsOf(const std::vector<RecipeKey> &a_keys) {
  std::vector<std::string> texts;
  for (const RecipeKey &key : a_keys) {
    const FormRef *form = key.Form();
    texts.push_back(form ? form->text : std::string{});
  }
  return texts;
}

void TestOrder() {
  const std::vector<WornPieceRow> ordered = OrderedWornPieces({
      Piece(0x30, "iron helmet"),
      Piece(0x10, "Steel Boots"),
      Piece(0x20, "Iron Gauntlets"),
      Piece(0x10, "Steel Boots"),
      Piece(0x40, "iron helmet"),
  });
  Check(ordered.size() == 4, "OrderedWornPieces drops a repeated armor");
  Check(ordered.size() == 4 && ordered[0].armor == Form(0x20) &&
            ordered[1].armor == Form(0x30) && ordered[2].armor == Form(0x40) &&
            ordered[3].armor == Form(0x10),
        "OrderedWornPieces sorts by name without case, then by form");
}

void TestLabel() {
  WornPieceRow plain = Piece(0x10, "Steel Boots");
  Check(WornPieceLabel(plain) == "Steel Boots (unenchanted)",
        "an unenchanted piece says so");
  plain.enchantment = "Fortify Sneak";
  Check(WornPieceLabel(plain) == "Steel Boots (Fortify Sneak)",
        "an enchanted piece names its enchantment");
  const WornPieceRow nameless =
      Piece(0x10, "", {Choice(KeyKind::kArmor, 0x10, "ArmorSteelBoots")});
  Check(WornPieceLabel(nameless) == "ArmorSteelBoots (unenchanted)",
        "a nameless piece falls back to its armor key text");
}

void TestArmorKey() {
  const WornPieceRow piece =
      Piece(0x10, "Steel Boots",
            {Choice(KeyKind::kKeyword, 0x1, "ArmorBoots"),
             Choice(KeyKind::kArmor, 0x10, "ArmorSteelBoots")});
  const std::optional<RecipeKey> key = ArmorKeyOf(piece);
  Check(key && key->kind == KeyKind::kArmor && key->Form() &&
            key->Form()->text == "ArmorSteelBoots" && key->Form()->key &&
            *key->Form()->key == Form(0x10),
        "ArmorKeyOf keys the recipe by the piece's armor form");
  Check(!ArmorKeyOf(Piece(0x10, "Steel Boots")),
        "a piece without an armor choice has no armor key");
}

void TestKeywordsToAdd() {
  const std::vector<KeyChoice> offered{
      Choice(KeyKind::kArmor, 0x10, "ArmorSteelBoots"),
      Choice(KeyKind::kKeyword, 0x1, "ArmorBoots"),
      Choice(KeyKind::kKeyword, 0x2, "ArmorHeavy"),
      Choice(KeyKind::kKeyword, 0x3, "ArmorMaterialSteel"),
      Choice(KeyKind::kKeyword, 0x2, "ArmorHeavy"),
      Choice(KeyKind::kEnchantment, 0x99, "EnchFortifySneak"),
  };
  const std::vector<RecipeKey> none{};
  Check(TextsOf(KeywordKeysToAdd(none, offered)) ==
            std::vector<std::string>{"ArmorBoots", "ArmorHeavy",
                                     "ArmorMaterialSteel"},
        "every keyword is added once, in the piece's order");

  RecipeKey armor;
  armor.kind = KeyKind::kArmor;
  armor.operand = FormRef::From("ArmorSteelBoots");
  RecipeKey heavyByText;
  heavyByText.kind = KeyKind::kKeyword;
  heavyByText.operand = FormRef::From("armorheavy");
  const std::vector<RecipeKey> have{armor, heavyByText, Keyword(0x3, "Steel")};
  Check(TextsOf(KeywordKeysToAdd(have, offered)) ==
            std::vector<std::string>{"ArmorBoots"},
        "a keyword the recipe has, by text or by form, is not added again");

  const std::vector<RecipeKey> added = KeywordKeysToAdd(none, offered);
  Check(!added.empty() && added[0].kind == KeyKind::kKeyword &&
            added[0].Form() && added[0].Form()->key &&
            *added[0].Form()->key == Form(0x1),
        "an added keyword key carries the resolved form");

  const std::vector<KeyChoice> bare{
      Choice(KeyKind::kArmor, 0x10, "ArmorSteelBoots")};
  Check(KeywordKeysToAdd(none, bare).empty(),
        "a piece without keywords adds nothing");
  Check(!HasKeywords(Piece(0x10, "Steel Boots", bare)),
        "HasKeywords is false for a piece without keywords");
  Check(HasKeywords(Piece(0x10, "Steel Boots", offered)),
        "HasKeywords is true for a piece with a keyword");
}

void TestOfferLabel() {
  const WornPieceRow bare = Piece(0x10, "Steel Boots");
  const WornPieceRow keyed = Piece(
      0x10, "Steel Boots", {Choice(KeyKind::kKeyword, 0x1, "ArmorBoots")});
  Check(KeywordOfferLabel(bare, 0) == "Steel Boots (unenchanted): no keywords",
        "a piece without keywords says so");
  Check(KeywordOfferLabel(keyed, 0) ==
            "Steel Boots (unenchanted): every keyword is already a key",
        "a piece whose keywords are all keys says so");
  Check(KeywordOfferLabel(keyed, 1) ==
            "Steel Boots (unenchanted): add 1 keyword(s)",
        "a piece with new keywords counts them");
}
}

int main() {
  TestOrder();
  TestLabel();
  TestArmorKey();
  TestKeywordsToAdd();
  TestOfferLabel();
  return test::Finish("studio_wornpieces");
}
