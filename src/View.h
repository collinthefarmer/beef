#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR::Studio
{
	using FormID = std::uint32_t;

	struct PieceRef
	{
		FormID actorID = 0;
		FormID armorID = 0;
		bool   firstPerson = false;
		[[nodiscard]] bool operator==(const PieceRef&) const = default;
	};

	struct Pin
	{
		PieceRef    piece;
		std::string recipeID;
		[[nodiscard]] bool operator==(const Pin&) const = default;
	};

	struct LayerKey
	{
		std::string recipeID;
		std::size_t output = 0;
		std::size_t layer = 0;
		[[nodiscard]] auto operator<=>(const LayerKey&) const = default;
	};

	struct View
	{
		bool               freeze = false;
		float              scrubSeconds = 0.0f;
		float              speed = 1.0f;
		std::string        isolateRecipe;
		int                isolateOutput = -1;
		int                isolateLayer = -1;
		bool               isolatedBySolo = false;
		std::set<LayerKey> muted;
		std::optional<Pin> pin;

		[[nodiscard]] bool Isolating() const noexcept { return !isolateRecipe.empty(); }

		[[nodiscard]] std::vector<std::string> RecipeIDs() const;
		void                                   RenameRecipe(std::string_view a_from, std::string_view a_to);
		void                                   ForgetRecipe(std::string_view a_id);

		[[nodiscard]] bool RecipeShown(const std::string& a_recipe) const noexcept
		{
			return !Isolating() || isolateRecipe == a_recipe;
		}

		[[nodiscard]] bool OutputShown(const std::string& a_recipe, std::size_t a_output) const noexcept
		{
			return RecipeShown(a_recipe) && (isolateOutput < 0 || static_cast<std::size_t>(isolateOutput) == a_output);
		}

		[[nodiscard]] bool LayerShown(const std::string& a_recipe, std::size_t a_output, std::size_t a_layer) const
		{
			if (!OutputShown(a_recipe, a_output)) {
				return false;
			}
			if (isolateLayer >= 0 && isolateRecipe == a_recipe && isolateOutput >= 0 && static_cast<std::size_t>(isolateOutput) == a_output) {
				return static_cast<std::size_t>(isolateLayer) == a_layer;
			}
			return !muted.contains(LayerKey{ a_recipe, a_output, a_layer });
		}

		[[nodiscard]] bool LayerMuted(const std::string& a_recipe, std::size_t a_output, std::size_t a_layer) const
		{
			return muted.contains(LayerKey{ a_recipe, a_output, a_layer });
		}

		[[nodiscard]] bool FiltersLayers(const std::string& a_recipe, std::size_t a_output) const
		{
			if (isolateLayer >= 0 && isolateRecipe == a_recipe && isolateOutput >= 0 && static_cast<std::size_t>(isolateOutput) == a_output) {
				return true;
			}
			for (const auto& key : muted) {
				if (key.recipeID == a_recipe && key.output == a_output) {
					return true;
				}
			}
			return false;
		}
	};
}
