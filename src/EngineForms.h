#pragma once

// Form identity on the engine side, shared by the store, the manager and
// the environment: how a loaded form becomes a form key or an editor ID and
// back, and how an effect shader becomes the importer's record. Every
// lookup can fail and says so with an optional or an empty string.

#include "Importer.h"
#include "PCH.h"
#include "Recipe.h"

#include <optional>
#include <string>

namespace WornEnchantmentPBR
{
	// Local id plus plugin file, as recipes write it.
	[[nodiscard]] FormKey FormKeyFor(const RE::TESForm& a_form);

	// The engine's own editor ID when the form type keeps one, else po3's
	// Tweaks export (which SPID and KID rely on), else empty.
	[[nodiscard]] std::string EditorIdOf(const RE::TESForm& a_form);

	// Whether the Tweaks export is available this session.
	[[nodiscard]] bool TweaksEditorIdsAvailable();

	// The loaded form for a key, if the plugin is loaded and the id exists.
	[[nodiscard]] RE::TESForm* LookupForm(const FormKey& a_key);

	template <class Form>
	[[nodiscard]] Form* LookupForm(const FormKey& a_key)
	{
		auto* form = LookupForm(a_key);
		return form ? form->As<Form>() : nullptr;
	}

	// The fields the importer and the efsh signals read.
	[[nodiscard]] EffectShaderRecord RecordFrom(const RE::TESEffectShader& a_shader);

	// The effect shader an enchantment shows on worn armor, chosen the way
	// Visible Armor Enchantments does: the costliest effect's enchant visuals,
	// then its enchant shader, then the first effect with either
	// (decompiled/WornEnchantmentFX/plugin.c 2013-2039).
	[[nodiscard]] RE::TESEffectShader* ShaderFor(const RE::EffectSetting* a_effect);
	[[nodiscard]] RE::TESEffectShader* ShaderFor(const RE::MagicItem* a_item);
}
