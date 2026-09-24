// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once
#include <memory>

namespace BetterEnchantmentEffects {
struct SkinPaletteState;
class SkinPaletteLease {
public:
  [[nodiscard]] static std::unique_ptr<SkinPaletteLease>
  Preserve(RE::BSGeometry &a_source, RE::BSGeometry &a_clone);
  ~SkinPaletteLease();
  SkinPaletteLease(const SkinPaletteLease &) = delete;
  SkinPaletteLease &operator=(const SkinPaletteLease &) = delete;
  [[nodiscard]] bool StillOwned(const RE::BSGeometry &a_clone) const noexcept;

private:
  SkinPaletteLease();
  std::unique_ptr<SkinPaletteState> state_;
};
}
