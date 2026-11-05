// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
struct PeerMessage {
  std::string problem;
  std::string action;

  [[nodiscard]] std::string Line() const { return problem + " " + action; }
};

struct RuntimeFacts {
  std::string_view runtime;
  std::string_view skse;
  std::string_view supported;
  std::string_view minimumSkse;
};

[[nodiscard]] PeerMessage CommunityShadersMissing();
[[nodiscard]] PeerMessage CommunityShadersLayoutUnknown();
[[nodiscard]] PeerMessage RuntimeRefused(const RuntimeFacts &a_facts);
}
