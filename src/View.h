#pragma once

// How the piece is being looked at, as opposed to what the recipe says:
// freeze and scrub, isolate a recipe, an output or a layer, mute layers.
// Held by the manager, set by the menu, read by the tick. Nothing here is
// ever written to a file.

#include <compare>
#include <cstddef>
#include <set>
#include <string>

namespace WornEnchantmentPBR::Studio
{
	struct LayerKey
	{
		std::string recipe;
		std::size_t output = 0;
		std::size_t layer = 0;
		[[nodiscard]] auto operator<=>(const LayerKey&) const = default;
	};

	struct View
	{
		bool               freeze = false;
		float              scrubSeconds = 0.0f;
		float              speed = 1.0f;  // multiplies every recipe's clock while the studio runs it
		std::string        isolateRecipe;       // recipe id, empty = all
		int                isolateOutput = -1;  // output index in that recipe, -1 = all
		int                isolateLayer = -1;   // layer index in that output, -1 = all
		bool               isolatedBySolo = false;  // the recipe isolate came from an output or layer solo, so that solo turning off clears it
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

		// Solo (isolate layer) wins over mute; a layer outside the isolated
		// output is hidden with its output.
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

		// True when any layer of the output is hidden by solo or mute, so a
		// static stack knows to render again.
		[[nodiscard]] bool FiltersLayers(const std::string& a_recipe, std::size_t a_output) const
		{
			if (isolateLayer >= 0 && isolateRecipe == a_recipe && isolateOutput >= 0 && static_cast<std::size_t>(isolateOutput) == a_output) {
				return true;
			}
			for (const auto& key : muted) {
				if (key.recipe == a_recipe && key.output == a_output) {
					return true;
				}
			}
			return false;
		}
	};
}
