#pragma once
#include <memory>

namespace BetterEnchantmentEffects {
struct SkinPaletteState;
// Owns repaired links and the scene storage they reference. Destroy before
// releasing the clone; cleanup removes only links that still match our repair.
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
