// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TextureLeases.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  TextureLeases<int> leases;
  Check(!leases.Retain(99).generated, "unregistered textures remain static");
  auto producer = std::make_shared<int>(7);
  const std::weak_ptr<int> recycled = producer;
  leases.Register(42, 1, producer);
  Check(!leases.Register(42, 2, std::make_shared<int>(9)),
        "a live presenter cannot be assigned to another target");
  Check(!leases.Register(42, 2, producer),
        "a live target cannot silently advance its generation");
  auto material = leases.Retain(42);
  auto snapshot = leases.Retain(42);
  auto preview = snapshot;
  Check(material.generated && material.generation == 1 && *material.target == 7,
        "a consumer receives producer identity and generation");
  producer.reset();
  material.target.reset();
  Check(!recycled.expired(),
        "retiring the producer and material cannot recycle a snapshot");
  snapshot.target.reset();
  Check(!recycled.expired(),
        "queued preview work retains its source independently");
  preview.target.reset();
  Check(recycled.expired(), "the last consumer releases the target");
  const auto retired = leases.Retain(42);
  Check(retired.generated && !retired.target,
        "expired generated texture cannot become a static texture");
  auto next = std::make_shared<int>(8);
  leases.Register(42, 2, next);
  auto renewed = leases.Retain(42);
  Check(renewed.generation == 2 && renewed.target == next,
        "reacquisition has a new generation");
  return test::Finish("texture leases");
}
