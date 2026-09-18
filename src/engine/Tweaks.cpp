#include "engine/Tweaks.h"

namespace BetterEnchantmentEffects {
namespace {
using TweaksEditorId = const char *(*)(std::uint32_t a_formId);

TweaksEditorId TweaksLookup() {
  static const TweaksEditorId lookup = [] {
    const auto module = REX::W32::GetModuleHandleW(L"po3_Tweaks");
    return module ? reinterpret_cast<TweaksEditorId>(
                        REX::W32::GetProcAddress(module, "GetFormEditorID"))
                  : nullptr;
  }();
  return lookup;
}
}

std::string EditorIdOf(const RE::TESForm &a_form) {
  if (const char *own = a_form.GetFormEditorID(); own && *own) {
    return own;
  }
  if (const auto lookup = TweaksLookup()) {
    if (const char *id = lookup(a_form.GetFormID()); id && *id) {
      return id;
    }
  }
  return {};
}

bool TweaksEditorIdsAvailable() { return TweaksLookup() != nullptr; }
}
