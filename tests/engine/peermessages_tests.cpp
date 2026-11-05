// GPL-3.0-only with the additional permission in COPYING.md.
#include "Identity.h"
#include "engine/PeerMessages.h"
#include "test_support.h"

#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
bool Names(const std::string &a_text, std::string_view a_part) {
  return a_text.find(a_part) != std::string::npos;
}

void EveryMessageSaysWhatToDo() {
  for (const PeerMessage &message :
       {CommunityShadersMissing(), CommunityShadersLayoutUnknown(),
        RuntimeRefused({"1.4.15", "2.0.12", "Skyrim SE 1.5", "2.0.17"})}) {
    Check(!message.problem.empty() && Names(message.action, "Install"),
          "a peer message names the problem and an action");
    Check(Names(message.Line(), message.problem) &&
              Names(message.Line(), message.action),
          "the log line carries the problem and the action");
    Check(!Names(message.Line(), "emissive path") &&
              !Names(message.Line(), "NiSourceTexture"),
          "a peer message uses no internal term");
  }
}

void TheRuntimeMessageNamesBothSides() {
  const PeerMessage refused =
      RuntimeRefused({"1.4.15", "2.0.12", "Skyrim SE 1.5", "2.0.17"});
  Check(Names(refused.problem, "1.4.15") && Names(refused.problem, "2.0.12"),
        "the refusal names the running Skyrim and SKSE");
  Check(Names(refused.action, "Skyrim SE 1.5") &&
            Names(refused.action, "2.0.17"),
        "the refusal names what the build supports");
  Check(Names(refused.problem, Identity::kName),
        "the refusal names the plugin");
}
}

int main() {
  EveryMessageSaysWhatToDo();
  TheRuntimeMessageNamesBothSides();
  return test::Finish("engine_peermessages");
}
