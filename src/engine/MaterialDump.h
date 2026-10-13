// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/LiveActor.h"
#include "studio/Snapshot.h"

#include <expected>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::expected<Studio::MaterialDumpWritten, std::string>
WriteMaterialDump(const LivePiece &a_piece, const LiveGeometry &a_geometry,
                  std::string_view a_build);
}
