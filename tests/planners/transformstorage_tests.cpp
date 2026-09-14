#include "planners/TransformStorage.h"
#include "test_support.h"

#include <limits>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  Check(TransformStorageIndex(0x1034, {0x1000, 35, 0x80, 0x34}) == 0,
        "first embedded world transform belongs to the storage");
  Check(TransformStorageIndex(0x2134, {0x1000, 35, 0x80, 0x34}) == 34,
        "last entry belongs to the storage");
  Check(!TransformStorageIndex(0x21B4, {0x1000, 35, 0x80, 0x34}),
        "one-past-end cannot prove ownership");
  Check(!TransformStorageIndex(0x1035, {0x1000, 35, 0x80, 0x34}),
        "interior address is not a transform entry");
  Check(!TransformStorageIndex(0x1000, {0x1000, 35, 0x80, 0x34}),
        "local transform is not the world transform");
  Check(!TransformStorageIndex(0x1034, {0, 35, 0x80, 0x34}), "null owner");
  Check(!TransformStorageIndex(0, {0x1000, 35, 0x80, 0x34}), "null transform");
  Check(!TransformStorageIndex(0x1034, {0x1000, 0, 0x80, 0x34}), "empty owner");
  Check(!TransformStorageIndex(0x1034, {0x1000, 35, 0, 0x34}),
        "invalid stride");
  Check(!TransformStorageIndex(0x1034, {0x1000, 35, 0x30, 0x34}),
        "invalid offset");
  Check(!TransformStorageIndex(0x34, {0x1000, 35, 0x80, 0x34}),
        "address before owner");
  const auto maximum = std::numeric_limits<std::uintptr_t>::max();
  Check(!TransformStorageIndex(0x34, {maximum - 0x10, 35, 0x80, 0x34}),
        "wrapped address does not match an owner");
  Check(TransformStorageIndex(maximum - 0x4B,
                              {maximum - 0xFF, 2, 0x80, 0x34}) == 1,
        "valid ownership near address-space end avoids arithmetic overflow");
  return test::Finish("transform storage");
}
