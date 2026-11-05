// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/PeerMessages.h"

#include "Identity.h"

#include <format>

namespace BetterEnchantmentEffects {
PeerMessage CommunityShadersMissing() {
  return {
      std::format("Community Shaders is not loaded, so {} shows no effects.",
                  Identity::kName),
      "Install Community Shaders with TruePBR, enable it in your mod "
      "manager, and restart Skyrim. If it is enabled, skse64.log in the "
      "SKSE log folder says why it did not load."};
}

PeerMessage CommunityShadersLayoutUnknown() {
  return {std::format("{} does not recognize the PBR material layout of the "
                      "installed Community Shaders, so it shows no effects.",
                      Identity::kName),
          std::format("Install a Community Shaders release that the {0} "
                      "release notes list as tested, or a {0} update made for "
                      "your Community Shaders release, and restart Skyrim.",
                      Identity::kName)};
}

PeerMessage RuntimeRefused(const RuntimeFacts &a_facts) {
  return {std::format("{} does not load on Skyrim {} with SKSE {}.",
                      Identity::kName, a_facts.runtime, a_facts.skse),
          std::format("This build supports {} with SKSE {} or later. Install "
                      "the {} build made for your Skyrim version.",
                      a_facts.supported, a_facts.minimumSkse, Identity::kName)};
}
}
