#include "recipe/Recipe.h"

#include "recipe/Words.h"

#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects
{
	bool GlobMatch(std::string_view a_glob, std::string_view a_text) noexcept
	{
		const auto norm = [](char c) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return c == '\\' ? '/' : c;
		};
		std::size_t g = 0, t = 0, starG = std::string_view::npos, starT = 0;
		while (t < a_text.size()) {
			if (g < a_glob.size() && a_glob[g] == '*') {
				starG = g++;
				starT = t;
			} else if (g < a_glob.size() && norm(a_glob[g]) == norm(a_text[t])) {
				++g;
				++t;
			} else if (starG != std::string_view::npos) {
				g = starG + 1;
				t = ++starT;
			} else {
				return false;
			}
		}
		while (g < a_glob.size() && a_glob[g] == '*') {
			++g;
		}
		return g == a_glob.size();
	}

	bool Matches(const Selector& a_selector, const GeometryIdentity& a_geometry)
	{
		if (a_selector.All()) {
			return true;
		}
		return std::ranges::any_of(a_selector.anyOf, [&](const SelectorClause& t) {
			switch (t.kind) {
			case SelectorKind::kAddon: {
				const auto* form = t.Form();
				return form && form->key && a_geometry.addon && *a_geometry.addon == *form->key;
			}
			case SelectorKind::kGeometry:
				return GlobMatch(t.Glob(), a_geometry.name);
			case SelectorKind::kTexture:
				return GlobMatch(t.Glob(), a_geometry.diffusePath);
			}
			return false;
		});
	}

	namespace
	{
		bool FormKeyMatches(const RecipeKey& a_key, const KeyKindSpec& a_row, const WornPiece& a_piece)
		{
			const auto* form = a_key.Form();
			if (!form || !form->key) {
				return false;
			}
			if (a_row.singleForm) {
				const auto& have = a_piece.*a_row.singleForm;
				return have && *have == *form->key;
			}
			if (a_row.formList) {
				return std::ranges::any_of(a_piece.*a_row.formList, [&](const FormKey& k) { return k == *form->key; });
			}
			return false;
		}

		bool KeyMatches(const RecipeKey& a_key, const WornPiece& a_piece)
		{
			const auto* row = RowOf(kKeyKinds, a_key.kind);
			if (!row) {
				return false;
			}
			switch (row->operand) {
			case KeyOperand::kNone:
				return true;
			case KeyOperand::kGlob:
				return std::ranges::any_of(a_piece.diffusePaths, [&](const std::string& p) { return GlobMatch(a_key.Glob(), p); });
			case KeyOperand::kForm:
				return FormKeyMatches(a_key, *row, a_piece);
			}
			return false;
		}
	}

	std::vector<ResolvedRecipe> Resolve(const WornPiece& a_piece, std::span<const Recipe> a_loaded)
	{
		struct Candidate
		{
			ResolvedRecipe resolved;
			std::size_t    loadIndex;
		};
		std::vector<RecipeKey> claimed;
		std::vector<Candidate> matches;
		bool                   enchantmentMatched = false;
		for (std::size_t i = a_loaded.size(); i-- > 0;) {
			const auto&              recipe = a_loaded[i];
			std::optional<RecipeKey> best;
			for (const auto& key : recipe.keys) {
				if (std::ranges::find(claimed, key) != claimed.end()) {
					continue;
				}
				claimed.push_back(key);
				if (!KeyMatches(key, a_piece)) {
					continue;
				}
				if (!best || DefaultPriority(key.kind) > DefaultPriority(best->kind)) {
					best = key;
				}
			}
			if (!best) {
				continue;
			}
			enchantmentMatched = enchantmentMatched || EnchantmentDerived(best->kind);
			matches.push_back({ { &recipe, *best, recipe.priority.value_or(DefaultPriority(best->kind)) }, i });
		}
		if (enchantmentMatched) {
			std::erase_if(matches, [](const Candidate& m) { return m.resolved.key.kind == KeyKind::kDefault; });
		}
		std::ranges::stable_sort(matches, [](const Candidate& a, const Candidate& b) {
			if (a.resolved.priority != b.resolved.priority) {
				return a.resolved.priority < b.resolved.priority;
			}
			return a.loadIndex < b.loadIndex;
		});
		std::vector<ResolvedRecipe> out;
		out.reserve(matches.size());
		for (const auto& m : matches) {
			out.push_back(m.resolved);
		}
		return out;
	}

	bool AnyUnenchantedKey(std::span<const Recipe> a_loaded) noexcept
	{
		return std::ranges::any_of(a_loaded, [](const Recipe& r) {
			return std::ranges::any_of(r.keys, [](const RecipeKey& k) {
				const auto* row = RowOf(kKeyKinds, k.kind);
				return row && row->operand != KeyOperand::kNone && !row->enchantmentDerived;
			});
		});
	}

	std::vector<PieceKey> KeyChoicesOf(const WornPiece& a_piece)
	{
		std::vector<PieceKey> out;
		for (std::size_t i = std::size(kKeyKinds); i-- > 0;) {
			const auto& row = kKeyKinds[i];
			if (row.operand != KeyOperand::kForm) {
				continue;
			}
			if (row.singleForm) {
				if (const auto& form = a_piece.*row.singleForm) {
					out.push_back({ row.value, *form });
				}
			} else if (row.formList) {
				for (const auto& form : a_piece.*row.formList) {
					out.push_back({ row.value, form });
				}
			}
		}
		return out;
	}

	const PieceKey* DefaultKeyChoice(std::span<const PieceKey> a_choices) noexcept
	{
		const auto armor = std::ranges::find(a_choices, KeyKind::kArmor, &PieceKey::kind);
		if (armor != a_choices.end()) {
			return &*armor;
		}
		return a_choices.empty() ? nullptr : &a_choices.front();
	}

	RecipeKey RecipeKeyOf(const PieceKey& a_key, std::string_view a_text)
	{
		RecipeKey key;
		key.kind = a_key.kind;
		key.operand = FormRef{ std::string{ a_text }, a_key.form };
		return key;
	}
}
