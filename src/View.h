#pragma once

#include <compare>
#include <cstddef>
#include <set>
#include <string>

namespace WornEnchantmentPBR::Studio
{
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

		[[nodiscard]] bool Isolating() const noexcept { return !isolateRecipe.empty(); }

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
