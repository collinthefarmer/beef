// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace logger {
template <class... Args> void error(std::string_view, const Args &...) {}
template <class... Args> void warn(std::string_view, const Args &...) {}
template <class... Args> void info(std::string_view, const Args &...) {}
}

namespace RE {
struct TESForm {
  static TESForm *LookupByEditorID(std::string_view) { return nullptr; }
};
struct TESEffectShader {
  std::uint32_t GetFormID() const { return 0; }
};
namespace MagicSystem {
enum class CastingType { kConstantEffect };
}
struct EnchantmentItem {
  MagicSystem::CastingType GetCastingType() const {
    return MagicSystem::CastingType::kConstantEffect;
  }
};
struct TESDataHandler {
  static TESDataHandler *GetSingleton() { return nullptr; }
  template <class T> std::vector<T *> GetFormArray() const { return {}; }
};
}
