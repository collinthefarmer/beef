#pragma once

#include "Importer.h"
#include "PCH.h"
#include "Recipe.h"

#include <optional>
#include <string>

namespace BetterEnchantmentEffects
{
	[[nodiscard]] FormKey FormKeyFor(const RE::TESForm& a_form);

	[[nodiscard]] std::string EditorIdOf(const RE::TESForm& a_form);

	[[nodiscard]] bool TweaksEditorIdsAvailable();

	[[nodiscard]] RE::TESForm* LookupForm(const FormKey& a_key);

	template <class Form>
	[[nodiscard]] Form* LookupForm(const FormKey& a_key)
	{
		auto* form = LookupForm(a_key);
		return form ? form->As<Form>() : nullptr;
	}

	[[nodiscard]] EffectShaderRecord RecordFrom(const RE::TESEffectShader& a_shader);

	[[nodiscard]] RE::TESEffectShader* ShaderFor(const RE::EffectSetting* a_effect);
	[[nodiscard]] RE::TESEffectShader* ShaderFor(const RE::MagicItem* a_item);
}
