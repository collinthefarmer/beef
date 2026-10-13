// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Relationships.h"

namespace BetterEnchantmentEffects::Studio {
LinkKind LinkKindOf(ResourceKind a_kind) noexcept {
  switch (a_kind) {
  case ResourceKind::kSource:
    return LinkKind::kSource;
  case ResourceKind::kMask:
    return LinkKind::kMask;
  case ResourceKind::kCurve:
    return LinkKind::kCurve;
  case ResourceKind::kSignal:
  case ResourceKind::kCount:
    return LinkKind::kSignal;
  }
  return LinkKind::kSignal;
}

LinkKind LinkKindOf(const RelationshipOwner &a_owner) noexcept {
  return Match(
      a_owner, [](const ResourceRef &a_ref) { return LinkKindOf(a_ref.kind); },
      [](const OutputOwner &) { return LinkKind::kOutput; },
      [](const LayerOwner &) { return LinkKind::kLayer; },
      [](const ShellOwner &) { return LinkKind::kShell; },
      [](const VariantOwner &) { return LinkKind::kVariant; });
}
}
