#pragma once
namespace BetterEnchantmentEffects {
// Copies every owned weight buffer; retains the shared skin partition.
[[nodiscard]] RE::NiPointer<RE::NiSkinData>
CopySkinData(const RE::NiSkinData &a_source);
}
